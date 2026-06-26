#pragma once

#include "CoreExport.hxx"

#include <coroutine>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace Engine {

class ThreadPoolScheduler;

// External threads enqueue unpinned work into per-worker inbound FIFO queues.
// WaitIdle drains unpinned inbound work on the submitting thread. Worker threads
// service pinned queues/deques and steal across stealable deques.
struct TaskOptions {
    // When set, enqueues on that worker's queue instead of round-robin.
    std::optional<std::size_t> target_worker{};
    // When target_worker is set: true = pinned (owner-only), false = stealable local queue.
    bool pinned = true;
};

struct Job {
    void (*fn)(void*) = nullptr;
    void* ctx = nullptr;

    void operator()() const {
        if (fn) {
            fn(ctx);
        }
    }

    explicit operator bool() const noexcept { return fn != nullptr; }
};

class GE_API ThreadPool {
public:
    explicit ThreadPool(std::size_t worker_count = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void Submit(Job job, TaskOptions options = {});

    void SubmitOnWorker(Job job, TaskOptions options = {});

    template<typename F>
    void Submit(F&& callable, TaskOptions options = {}) {
        using Callable = std::decay_t<F>;
        struct State {
            Callable callable;
        };
        auto* state = new State{std::forward<F>(callable)};
        Submit(
            Job{
                [](void* ctx) {
                    auto* owned = static_cast<State*>(ctx);
                    owned->callable();
                    delete owned;
                },
                state,
            },
            options);
    }

    void WaitIdle();
    void Shutdown(bool drain_pending = true);

    [[nodiscard]] std::size_t WorkerCount() const noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;

    [[nodiscard]] static std::optional<std::size_t> CurrentWorkerIndex() noexcept;

    void ResumeCoroutine(std::coroutine_handle<> handle, TaskOptions options = {});

    [[nodiscard]] ThreadPoolScheduler GetScheduler() noexcept;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace Engine
