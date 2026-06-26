#pragma once

#include "Task.hxx"
#include "ThreadPool.hxx"
#include "ThreadPoolScheduler.hxx"
#include "WorkerRenderContext.hxx"

#include <type_traits>
#include <utility>

namespace Engine {

// Enqueues fn on a pool worker and passes the thread-local WorkerRenderContext.
template<typename Fn>
void SubmitWithContext(ThreadPool& pool, Fn&& fn, TaskOptions options = {}) {
    using Callable = std::decay_t<Fn>;
    struct State {
        Callable callable;
    };
    auto* state = new State{std::forward<Fn>(fn)};
    pool.SubmitOnWorker(
        Job{
            [](void* ctx) {
                auto* owned = static_cast<State*>(ctx);
                WorkerRenderContext* render_context = WorkerRenderContext::Current();
                owned->callable(*render_context);
                delete owned;
            },
            state,
        },
        options);
}

struct WithWorkerContextAwaiter {
    ThreadPoolScheduler scheduler_;
    TaskOptions options_{};

    [[nodiscard]] bool await_ready() const noexcept {
        if (scheduler_.GetPool() == nullptr) {
            return true;
        }
        const auto worker = ThreadPool::CurrentWorkerIndex();
        if (!worker.has_value()) {
            return false;
        }
        if (options_.target_worker.has_value()) {
            return *options_.target_worker == *worker;
        }
        return WorkerRenderContext::Current() != nullptr;
    }

    void await_suspend(std::coroutine_handle<> handle) const {
        scheduler_.GetPool()->ResumeCoroutine(handle, options_);
    }

    [[nodiscard]] WorkerRenderContext& await_resume() const {
        return *WorkerRenderContext::Current();
    }
};

[[nodiscard]] inline WithWorkerContextAwaiter with_worker_context(
    ThreadPoolScheduler scheduler,
    TaskOptions options = {}) noexcept {
    return WithWorkerContextAwaiter{scheduler, options};
}

} // namespace Engine
