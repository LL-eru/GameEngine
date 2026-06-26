#include "Public/ThreadPool.hxx"
#include "Public/ThreadPoolScheduler.hxx"
#include "Public/EngineAllocator.hxx"
#include "Public/WorkerRenderContext.hxx"
#include "Public/WorkStealingDeque.hxx"
#include "Public/TestDiagnostics.hxx"
#include "../../Interface/MemoryAPI.hxx"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Engine {
namespace {

constexpr std::size_t kInvalidWorkerIndex = static_cast<std::size_t>(-1);
constexpr std::size_t kQueueCapacity = 4096;
constexpr std::size_t kPinnedCapacity = 256;

constexpr std::uint32_t kPoolType32 = 0;
constexpr std::uint32_t kPoolType256 = 1;
constexpr std::uint32_t kPoolTypeHeap = 2;

thread_local std::size_t tls_worker_index = kInvalidWorkerIndex;
thread_local FrameArena* tls_worker_frame_arena = nullptr;
thread_local GPUArena*   tls_worker_gpu_arena = nullptr;

struct TaskHeader {
    void (*invoker)(void*) = nullptr;
    void (*destructor)(void*) = nullptr;
    std::uint32_t pool_type = kPoolTypeHeap;
    std::uint32_t payload_size = 0;
};

PoolHandle s_task_pool_32 = nullptr;
PoolHandle s_task_pool_256 = nullptr;
std::mutex s_pool_mutex;

void RunTask(Job& job) {
    job();
}

void ResumeCoroutineHandle(void* ctx) {
    auto handle = std::coroutine_handle<>::from_address(ctx);
    if (handle && !handle.done()) {
        handle.resume();
    }
}

void* TaskPayload(void* block) {
    return static_cast<char*>(block) + sizeof(TaskHeader);
}

void ExecutePooledTask(void* ctx) {
    auto* header = static_cast<TaskHeader*>(ctx);
    header->invoker(TaskPayload(ctx));

    std::lock_guard lock(s_pool_mutex);
    if (header->pool_type == kPoolType32) {
        EngineAllocator::FreePool(s_task_pool_32, ctx);
    } else if (header->pool_type == kPoolType256) {
        EngineAllocator::FreePool(s_task_pool_256, ctx);
    } else {
        EngineAllocator::FreeHeap(ctx);
    }
}

void ExecuteFallbackTask(void* ctx) {
    auto* header = static_cast<TaskHeader*>(ctx);
    void* payload = TaskPayload(ctx);
    header->invoker(payload);
    if (header->destructor) {
        header->destructor(payload);
    }
    EngineAllocator::FreeHeap(ctx);
}

void InitializeTaskPools() {
    std::lock_guard lock(s_pool_mutex);
    if (!s_task_pool_32) {
        s_task_pool_32 = EngineAllocator::CreatePool(sizeof(TaskHeader) + 32, 1024);
        s_task_pool_256 = EngineAllocator::CreatePool(sizeof(TaskHeader) + 256, 128);
    }
}

std::size_t PoolFreeCount(PoolHandle pool) {
    std::lock_guard lock(s_pool_mutex);
    if (!pool) {
        return 0;
    }
    return pool->pool.GetFreeCount();
}

} // namespace

namespace detail {

std::size_t GetTaskPool32FreeCount() noexcept {
    return PoolFreeCount(s_task_pool_32);
}

std::size_t GetTaskPool256FreeCount() noexcept {
    return PoolFreeCount(s_task_pool_256);
}

} // namespace detail

struct ThreadPool::Impl {
    struct Worker {
        WorkStealingDeque<Job, kQueueCapacity> queue;
        WorkStealingDeque<Job, kPinnedCapacity> pinned;

        std::mutex inbound_mutex;
        std::deque<Job> inbound_queue;
        std::deque<Job> inbound_pinned;

        FrameArena local_frame_arena;
        GPUArena   local_gpu_arena;

        std::thread thread;
        std::thread::id thread_id{};
    };

    std::vector<std::unique_ptr<Worker>> workers;
    std::atomic<std::size_t> next_submit{0};
    std::atomic<std::size_t> pending{0};
    std::atomic<bool> running{false};
    std::atomic<bool> shutdown_requested{false};

    std::atomic<std::uint64_t> global_flush_generation{0};

    std::mutex wait_mutex;
    std::condition_variable wait_cv;
    std::condition_variable worker_cv;

    static Impl* s_active_impl;

    explicit Impl(std::size_t worker_count) {
        if (worker_count == 0) {
            worker_count = std::thread::hardware_concurrency();
        }
        if (worker_count == 0) {
            worker_count = 1;
        }

        InitializeTaskPools();
        s_active_impl = this;

        workers.reserve(worker_count);
        running.store(true, std::memory_order_release);
        for (std::size_t i = 0; i < worker_count; ++i) {
            auto worker = std::make_unique<Worker>();
            worker->local_frame_arena.Initialize(16 * 1024 * 1024);
            worker->local_gpu_arena.Initialize(64 * 1024 * 1024);
            worker->thread = std::thread([this, i] { WorkerLoop(i); });
            workers.push_back(std::move(worker));
        }
    }

    ~Impl() {
        if (running.load(std::memory_order_acquire)) {
            Shutdown(true);
        }
        for (const auto& worker : workers) {
            worker->local_gpu_arena.Shutdown();
            worker->local_frame_arena.Shutdown();
        }
        if (s_active_impl == this) {
            s_active_impl = nullptr;
        }
    }

    void WorkerLoop(std::size_t index) {
        tls_worker_index = index;
        tls_worker_frame_arena = &workers[index]->local_frame_arena;
        tls_worker_gpu_arena = &workers[index]->local_gpu_arena;
        workers[index]->thread_id = std::this_thread::get_id();

        WorkerRenderContext render_context(index);
        BindWorkerRenderContext(&render_context);

        std::size_t steal_victim = 0;
        std::uint64_t last_flushed_generation = 0;

        while (true) {
            if (shutdown_requested.load(std::memory_order_acquire)) {
                break;
            }

            if (TryExecuteLocal(index)) {
                continue;
            }

            if (TrySteal(index, steal_victim)) {
                continue;
            }

            if (pending.load(std::memory_order_acquire) == 0) {
                const std::uint64_t current_gen =
                    global_flush_generation.load(std::memory_order_acquire);
                if (current_gen > last_flushed_generation) {
                    FlushThreadCache();
                    TestDiagnostics::RecordWorkerFlush(index);
                    last_flushed_generation = current_gen;
                }

                std::unique_lock lock(wait_mutex);
                wait_cv.notify_all();
                worker_cv.wait_for(lock, std::chrono::microseconds(200), [this] {
                    return shutdown_requested.load(std::memory_order_acquire)
                        || pending.load(std::memory_order_acquire) > 0;
                });
                continue;
            }

            std::this_thread::yield();
        }

        BindWorkerRenderContext(nullptr);
        tls_worker_gpu_arena = nullptr;
        tls_worker_frame_arena = nullptr;
        tls_worker_index = kInvalidWorkerIndex;
    }

    bool TryPopInbound(Worker& worker, Job& out_job, bool pinned) {
        std::lock_guard lock(worker.inbound_mutex);
        auto& inbound = pinned ? worker.inbound_pinned : worker.inbound_queue;
        if (inbound.empty()) {
            return false;
        }
        out_job = std::move(inbound.front());
        inbound.pop_front();
        return true;
    }

    void HelpDrainUnpinnedInbound() {
        for (const auto& worker_ptr : workers) {
            Worker& worker = *worker_ptr;
            Job job{};
            while (TryPopInbound(worker, job, false)) {
                ExecuteJob(std::move(job));
                job = Job{};
            }
        }
    }

    bool TryExecuteLocal(std::size_t index) {
        Worker& worker = *workers[index];

        if (auto job = worker.pinned.Pop()) {
            ExecuteJob(std::move(*job));
            return true;
        }

        Job inbound_job{};
        if (TryPopInbound(worker, inbound_job, true)) {
            ExecuteJob(std::move(inbound_job));
            return true;
        }

        if (auto job = worker.queue.Pop()) {
            ExecuteJob(std::move(*job));
            return true;
        }

        return false;
    }

    bool TrySteal(std::size_t thief, std::size_t& victim_cursor) {
        const std::size_t count = workers.size();
        for (std::size_t step = 0; step < count; ++step) {
            const std::size_t victim = (thief + 1 + victim_cursor + step) % count;
            if (victim == thief) {
                continue;
            }
            if (auto job = workers[victim]->queue.Steal()) {
                victim_cursor = (victim_cursor + step + 1) % count;
                ExecuteJob(std::move(*job));
                return true;
            }
        }
        return false;
    }

    void ExecuteJob(Job job) {
        RunTask(job);
        const std::size_t remaining = pending.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) {
            std::lock_guard lock(wait_mutex);
            wait_cv.notify_all();
        }
    }

    void SubmitJob(Job job, TaskOptions options) {
        pending.fetch_add(1, std::memory_order_release);

        const std::size_t count = workers.size();
        const std::optional<std::size_t> self = CurrentWorkerIndex();

        std::size_t target = 0;
        bool pinned = false;
        if (options.target_worker.has_value()) {
            target = *options.target_worker;
            pinned = options.pinned;
        } else {
            target = next_submit.fetch_add(1, std::memory_order_relaxed) % count;
        }

        if (target >= count) {
            pending.fetch_sub(1, std::memory_order_acq_rel);
            return;
        }

        Worker& worker = *workers[target];
        const bool owner_submit = self.has_value() && *self == target;

        if (owner_submit) {
            const bool pushed = pinned ? worker.pinned.Push(std::move(job))
                                       : worker.queue.Push(std::move(job));
            if (!pushed) {
                pending.fetch_sub(1, std::memory_order_acq_rel);
                return;
            }
        } else {
            std::lock_guard lock(worker.inbound_mutex);
            if (pinned) {
                worker.inbound_pinned.push_back(std::move(job));
            } else {
                worker.inbound_queue.push_back(std::move(job));
            }
        }

        worker_cv.notify_all();
    }

    void WaitIdle() {
        while (pending.load(std::memory_order_acquire) > 0) {
            HelpDrainUnpinnedInbound();

            std::unique_lock lock(wait_mutex);
            if (pending.load(std::memory_order_acquire) == 0) {
                break;
            }
            wait_cv.wait_for(lock, std::chrono::microseconds(200));
        }
    }

    void Shutdown(bool drain_pending) {
        if (!running.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        if (drain_pending) {
            WaitIdle();
        }

        shutdown_requested.store(true, std::memory_order_release);
        worker_cv.notify_all();

        for (const auto& worker : workers) {
            if (worker->thread.joinable()) {
                worker->thread.join();
            }
        }
    }
};

ThreadPool::Impl* ThreadPool::Impl::s_active_impl = nullptr;

ThreadPool::ThreadPool(std::size_t worker_count)
    : impl_(new Impl(worker_count)) {}

ThreadPool::~ThreadPool() {
    delete impl_;
}

void ThreadPool::SubmitInternal(
    void* src_callable,
    void (*invoker)(void*),
    void (*constructor)(void*, void*),
    std::size_t lambda_size,
    TaskOptions options) {
    if (!impl_->running.load(std::memory_order_acquire)) {
        return;
    }

    InitializeTaskPools();

    PoolHandle pool = (lambda_size <= 32) ? s_task_pool_32 : s_task_pool_256;
    std::uint32_t pool_type = (lambda_size <= 32) ? kPoolType32 : kPoolType256;

    void* mem = nullptr;
    {
        std::lock_guard lock(s_pool_mutex);
        mem = EngineAllocator::AllocPool(pool);
    }
    if (!mem) {
        mem = EngineAllocator::AllocHeap(sizeof(TaskHeader) + lambda_size, alignof(std::max_align_t));
        pool_type = kPoolTypeHeap;
    }
    if (!mem) {
        return;
    }

    auto* header = static_cast<TaskHeader*>(mem);
    header->invoker = invoker;
    header->destructor = nullptr;
    header->pool_type = pool_type;
    header->payload_size = static_cast<std::uint32_t>(lambda_size);

    constructor(TaskPayload(mem), src_callable);

    impl_->SubmitJob(Job{ExecutePooledTask, mem}, options);
}

void ThreadPool::SubmitFallbackInternal(
    void* src_callable,
    void (*invoker)(void*),
    void (*destructor)(void*),
    void (*constructor)(void*, void*),
    std::size_t lambda_size,
    TaskOptions options) {
    if (!impl_->running.load(std::memory_order_acquire)) {
        return;
    }

    void* mem = EngineAllocator::AllocHeap(sizeof(TaskHeader) + lambda_size, alignof(std::max_align_t));
    if (!mem) {
        return;
    }

    auto* header = static_cast<TaskHeader*>(mem);
    header->invoker = invoker;
    header->destructor = destructor;
    header->pool_type = kPoolTypeHeap;
    header->payload_size = static_cast<std::uint32_t>(lambda_size);

    constructor(TaskPayload(mem), src_callable);

    impl_->SubmitJob(Job{ExecuteFallbackTask, mem}, options);
}

void ThreadPool::Submit(Job job, TaskOptions options) {
    if (!impl_->running.load(std::memory_order_acquire)) {
        return;
    }
    impl_->SubmitJob(std::move(job), options);
}

void ThreadPool::SubmitOnWorker(Job job, TaskOptions options) {
    if (!options.target_worker.has_value()) {
        const std::size_t count = impl_->workers.size();
        options.target_worker = impl_->next_submit.fetch_add(1, std::memory_order_relaxed) % count;
        options.pinned = true;
    }
    Submit(std::move(job), options);
}

void ThreadPool::WaitIdle() {
    impl_->WaitIdle();
}

void ThreadPool::Shutdown(bool drain_pending) {
    impl_->Shutdown(drain_pending);
}

std::size_t ThreadPool::WorkerCount() const noexcept {
    return impl_->workers.size();
}

bool ThreadPool::IsRunning() const noexcept {
    return impl_->running.load(std::memory_order_acquire);
}

std::size_t ThreadPool::WorkerFrameUsedBytes(std::size_t worker_index) const noexcept {
    if (worker_index >= impl_->workers.size()) {
        return 0;
    }
    return impl_->workers[worker_index]->local_frame_arena.GetUsedBytes();
}

std::size_t ThreadPool::WorkerGpuUsedBytes(std::size_t worker_index) const noexcept {
    if (worker_index >= impl_->workers.size()) {
        return 0;
    }
    return impl_->workers[worker_index]->local_gpu_arena.GetUsedBytes();
}

std::optional<std::size_t> ThreadPool::CurrentWorkerIndex() noexcept {
    if (tls_worker_index == kInvalidWorkerIndex) {
        return std::nullopt;
    }
    return tls_worker_index;
}

FrameArena* ThreadPool::CurrentWorkerFrameArena() noexcept {
    return tls_worker_frame_arena;
}

GPUArena* ThreadPool::CurrentWorkerGPUArena() noexcept {
    return tls_worker_gpu_arena;
}

void ThreadPool::ResetAllWorkerArenas() noexcept {
    if (!Impl::s_active_impl) {
        return;
    }
    for (const auto& worker : Impl::s_active_impl->workers) {
        worker->local_frame_arena.Reset();
        worker->local_gpu_arena.Reset();
    }
}

void ThreadPool::IncrementFlushGeneration() noexcept {
    if (!Impl::s_active_impl) {
        return;
    }
    Impl::s_active_impl->global_flush_generation.fetch_add(1, std::memory_order_release);
}

std::uint64_t ThreadPool::GlobalFlushGeneration() noexcept {
    if (!Impl::s_active_impl) {
        return 0;
    }
    return Impl::s_active_impl->global_flush_generation.load(std::memory_order_acquire);
}

void ThreadPool::ResumeCoroutine(std::coroutine_handle<> handle, TaskOptions options) {
    if (!handle) {
        return;
    }
    SubmitOnWorker(Job{ResumeCoroutineHandle, handle.address()}, options);
}

ThreadPoolScheduler ThreadPool::GetScheduler() noexcept {
    return ThreadPoolScheduler{this};
}

} // namespace Engine
