#include "Public/ThreadPool.hxx"
#include "Public/ThreadPoolScheduler.hxx"
#include "Public/WorkerRenderContext.hxx"
#include "Public/WorkStealingDeque.hxx"

#include <atomic>
#include <coroutine>
#include <chrono>
#include <condition_variable>
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

void RunTask(Job& job) {
    job();
}

void ResumeCoroutineHandle(void* ctx) {
    auto handle = std::coroutine_handle<>::from_address(ctx);
    if (handle && !handle.done()) {
        handle.resume();
    }
}

} // namespace

struct ThreadPool::Impl {
    struct Worker {
        WorkStealingDeque<Job, kQueueCapacity> queue;
        WorkStealingDeque<Job, kPinnedCapacity> pinned;

        std::mutex inbound_mutex;
        std::deque<Job> inbound_queue;
        std::deque<Job> inbound_pinned;

        std::thread thread;
        std::thread::id thread_id{};
    };

    std::vector<std::unique_ptr<Worker>> workers;
    std::atomic<std::size_t> next_submit{0};
    std::atomic<std::size_t> pending{0};
    std::atomic<bool> running{false};
    std::atomic<bool> shutdown_requested{false};

    std::mutex wait_mutex;
    std::condition_variable wait_cv;
    std::condition_variable worker_cv;

    explicit Impl(std::size_t worker_count) {
        if (worker_count == 0) {
            worker_count = std::thread::hardware_concurrency();
        }
        if (worker_count == 0) {
            worker_count = 1;
        }
        workers.reserve(worker_count);
        running.store(true, std::memory_order_release);
        for (std::size_t i = 0; i < worker_count; ++i) {
            auto worker = std::make_unique<Worker>();
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
        workers[index]->thread_id = std::this_thread::get_id();

        WorkerRenderContext render_context(index);
        BindWorkerRenderContext(&render_context);

        std::size_t steal_victim = 0;

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

ThreadPool::ThreadPool(std::size_t worker_count)
    : impl_(new Impl(worker_count)) {}

ThreadPool::~ThreadPool() {
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

std::optional<std::size_t> ThreadPool::CurrentWorkerIndex() noexcept {
    if (tls_worker_index == kInvalidWorkerIndex) {
        return std::nullopt;
    }
    return tls_worker_index;
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
