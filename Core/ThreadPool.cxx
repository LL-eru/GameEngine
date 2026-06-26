#include "Public/ThreadPool.hxx"
#include "Public/ThreadPoolScheduler.hxx"
#include "Public/EngineAllocator.hxx"
#include "Public/WorkerRenderContext.hxx"
#include "Public/WorkStealingDeque.hxx"
#include "Public/TestDiagnostics.hxx"
#include "../../Interface/MemoryAPI.hxx"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
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

thread_local std::size_t tls_worker_index = kInvalidWorkerIndex;
thread_local ThreadPool* tls_current_pool = nullptr;

PoolHandle s_task_pool_32 = nullptr;
PoolHandle s_task_pool_256 = nullptr;
std::mutex s_pool_mutex;

std::mutex g_registered_pools_mutex;
std::vector<ThreadPool*> g_registered_pools;

void RunTask(Job& job) {
    job();
}

void ResumeCoroutineHandle(void* ctx) {
    auto handle = std::coroutine_handle<>::from_address(ctx);
    if (handle && !handle.done()) {
        handle.resume();
    }
}

void InitializeTaskPools() {
    std::lock_guard lock(s_pool_mutex);
    if (!s_task_pool_32) {
        s_task_pool_32 = EngineAllocator::CreatePool(32, 1024);
        s_task_pool_256 = EngineAllocator::CreatePool(256, 128);
    }
}

void RegisterPool(ThreadPool* pool) {
    std::lock_guard lock(g_registered_pools_mutex);
    g_registered_pools.push_back(pool);
}

void UnregisterPool(ThreadPool* pool) {
    std::lock_guard lock(g_registered_pools_mutex);
    const auto it = std::find(g_registered_pools.begin(), g_registered_pools.end(), pool);
    if (it != g_registered_pools.end()) {
        g_registered_pools.erase(it);
    }
}

} // namespace

namespace detail {

void* AllocTaskStorage32(bool* heap_fallback) noexcept {
    InitializeTaskPools();
    *heap_fallback = false;

    void* ctx_mem = nullptr;
    {
        std::lock_guard lock(s_pool_mutex);
        ctx_mem = EngineAllocator::AllocPool(s_task_pool_32);
    }
    if (!ctx_mem) {
        ctx_mem = EngineAllocator::AllocHeap(32, alignof(std::max_align_t));
        *heap_fallback = ctx_mem != nullptr;
    }
    return ctx_mem;
}

void FreeTaskStorage32(void* ptr, bool heap_fallback) noexcept {
    if (!ptr) {
        return;
    }
    if (heap_fallback) {
        EngineAllocator::FreeHeap(ptr);
        return;
    }
    std::lock_guard lock(s_pool_mutex);
    EngineAllocator::FreePool(s_task_pool_32, ptr);
}

void* AllocTaskStorage256(bool* heap_fallback) noexcept {
    InitializeTaskPools();
    *heap_fallback = false;

    void* ctx_mem = nullptr;
    {
        std::lock_guard lock(s_pool_mutex);
        ctx_mem = EngineAllocator::AllocPool(s_task_pool_256);
    }
    if (!ctx_mem) {
        ctx_mem = EngineAllocator::AllocHeap(256, alignof(std::max_align_t));
        *heap_fallback = ctx_mem != nullptr;
    }
    return ctx_mem;
}

void FreeTaskStorage256(void* ptr, bool heap_fallback) noexcept {
    if (!ptr) {
        return;
    }
    if (heap_fallback) {
        EngineAllocator::FreeHeap(ptr);
        return;
    }
    std::lock_guard lock(s_pool_mutex);
    EngineAllocator::FreePool(s_task_pool_256, ptr);
}

std::size_t GetTaskPool32FreeCount() noexcept {
    std::lock_guard lock(s_pool_mutex);
    if (!s_task_pool_32) {
        return 0;
    }
    return s_task_pool_32->pool.GetFreeCount();
}

std::size_t GetTaskPool256FreeCount() noexcept {
    std::lock_guard lock(s_pool_mutex);
    if (!s_task_pool_256) {
        return 0;
    }
    return s_task_pool_256->pool.GetFreeCount();
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

    ThreadPool* owner = nullptr;

    std::vector<std::unique_ptr<Worker>> workers;
    std::atomic<std::size_t> next_submit{0};
    std::atomic<std::size_t> pending{0};
    std::atomic<bool> running{false};
    std::atomic<bool> shutdown_requested{false};

    std::atomic<std::uint64_t> global_flush_generation{0};

    std::mutex wait_mutex;
    std::condition_variable wait_cv;
    std::condition_variable worker_cv;

    explicit Impl(std::size_t worker_count, ThreadPool* owner_pool) : owner(owner_pool) {
        if (worker_count == 0) {
            worker_count = std::thread::hardware_concurrency();
        }
        if (worker_count == 0) {
            worker_count = 1;
        }

        AllocatorConfig config{};
        workers.reserve(worker_count);
        running.store(true, std::memory_order_release);
        for (std::size_t i = 0; i < worker_count; ++i) {
            auto worker = std::make_unique<Worker>();
            worker->local_frame_arena.Initialize(config.frameArenaCapacityBytes);
            worker->local_gpu_arena.Initialize(config.gpuArenaCapacityBytes);
            worker->thread = std::thread([this, i] { WorkerLoop(i); });
            workers.push_back(std::move(worker));
        }
    }

    ~Impl() {
        if (running.load(std::memory_order_acquire)) {
            Shutdown(true);
        }
    }

    void WorkerLoop(std::size_t index) {
        tls_worker_index = index;
        tls_current_pool = owner;
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
        tls_worker_index = kInvalidWorkerIndex;
        tls_current_pool = nullptr;
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

    void ResetWorkerFrameArenas() {
        for (const auto& worker : workers) {
            worker->local_frame_arena.Reset();
            worker->local_gpu_arena.Reset();
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
            worker->local_frame_arena.Shutdown();
            worker->local_gpu_arena.Shutdown();
        }
    }
};

ThreadPool::ThreadPool(std::size_t worker_count)
    : impl_(new Impl(worker_count, this)) {
    RegisterPool(this);
}

ThreadPool::~ThreadPool() {
    UnregisterPool(this);
    delete impl_;
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
    const auto idx = CurrentWorkerIndex();
    if (!idx.has_value() || tls_current_pool == nullptr) {
        return nullptr;
    }
    auto& workers = tls_current_pool->impl_->workers;
    if (*idx >= workers.size()) {
        return nullptr;
    }
    return &workers[*idx]->local_frame_arena;
}

GPUArena* ThreadPool::CurrentWorkerGpuArena() noexcept {
    const auto idx = CurrentWorkerIndex();
    if (!idx.has_value() || tls_current_pool == nullptr) {
        return nullptr;
    }
    auto& workers = tls_current_pool->impl_->workers;
    if (*idx >= workers.size()) {
        return nullptr;
    }
    return &workers[*idx]->local_gpu_arena;
}

void ThreadPool::IncrementFlushGeneration() noexcept {
    impl_->global_flush_generation.fetch_add(1, std::memory_order_release);
}

std::uint64_t ThreadPool::GlobalFlushGeneration() noexcept {
    std::lock_guard lock(g_registered_pools_mutex);
    if (g_registered_pools.empty()) {
        return 0;
    }
    return g_registered_pools.front()->impl_->global_flush_generation.load(std::memory_order_acquire);
}

void ThreadPool::ResetAllWorkerFrameArenas() noexcept {
    std::lock_guard lock(g_registered_pools_mutex);
    for (ThreadPool* pool : g_registered_pools) {
        if (pool != nullptr && pool->impl_ != nullptr) {
            pool->impl_->ResetWorkerFrameArenas();
        }
    }
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
