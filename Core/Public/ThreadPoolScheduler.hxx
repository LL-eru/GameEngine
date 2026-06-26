#pragma once

#include "ThreadPool.hxx"

#include <coroutine>

namespace Engine {

class ThreadPoolScheduler {
public:
    explicit ThreadPoolScheduler(ThreadPool* pool) noexcept : pool_(pool) {}

    [[nodiscard]] ThreadPool* GetPool() const noexcept { return pool_; }

private:
    ThreadPool* pool_{nullptr};
};

struct SwitchToAwaiter {
    ThreadPool* pool_{nullptr};
    TaskOptions options_{};

    [[nodiscard]] bool await_ready() const noexcept {
        if (pool_ == nullptr) {
            return true;
        }
        const auto worker = ThreadPool::CurrentWorkerIndex();
        if (!worker.has_value()) {
            return false;
        }
        if (options_.target_worker.has_value()) {
            return *options_.target_worker == *worker;
        }
        return true;
    }

    void await_suspend(std::coroutine_handle<> handle) const {
        pool_->ResumeCoroutine(handle, options_);
    }

    void await_resume() const noexcept {}
};

[[nodiscard]] inline SwitchToAwaiter switch_to(
    ThreadPoolScheduler scheduler,
    TaskOptions options = {}) noexcept {
    return SwitchToAwaiter{scheduler.GetPool(), options};
}

} // namespace Engine
