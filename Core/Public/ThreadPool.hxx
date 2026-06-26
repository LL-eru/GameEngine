#pragma once

#include "CoreExport.hxx"

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

class FrameArena;
class GPUArena;

namespace Engine {

class ThreadPoolScheduler;

namespace detail {

void* AllocTaskStorage32(bool* heap_fallback) noexcept;
void  FreeTaskStorage32(void* ptr, bool heap_fallback) noexcept;
void* AllocTaskStorage256(bool* heap_fallback) noexcept;
void  FreeTaskStorage256(void* ptr, bool heap_fallback) noexcept;

std::size_t GetTaskPool32FreeCount() noexcept;
std::size_t GetTaskPool256FreeCount() noexcept;

} // namespace detail

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
        constexpr std::size_t kSize = sizeof(Callable);

        if constexpr (kSize <= 32) {
            bool heap = false;
            void* ctx = detail::AllocTaskStorage32(&heap);
            if (ctx == nullptr) {
                return;
            }
            new (ctx) Callable(std::forward<F>(callable));

            void (*trampoline)(void*) = nullptr;
            if (heap) {
                trampoline = +[](void* ctx_ptr) {
                    auto* storage = static_cast<Callable*>(ctx_ptr);
                    (*storage)();
                    storage->~Callable();
                    detail::FreeTaskStorage32(ctx_ptr, true);
                };
            } else {
                trampoline = +[](void* ctx_ptr) {
                    auto* storage = static_cast<Callable*>(ctx_ptr);
                    (*storage)();
                    storage->~Callable();
                    detail::FreeTaskStorage32(ctx_ptr, false);
                };
            }

            Submit(Job{trampoline, ctx}, options);
        } else if constexpr (kSize <= 256) {
            bool heap = false;
            void* ctx = detail::AllocTaskStorage256(&heap);
            if (ctx == nullptr) {
                return;
            }
            new (ctx) Callable(std::forward<F>(callable));

            void (*trampoline)(void*) = nullptr;
            if (heap) {
                trampoline = +[](void* ctx_ptr) {
                    auto* storage = static_cast<Callable*>(ctx_ptr);
                    (*storage)();
                    storage->~Callable();
                    detail::FreeTaskStorage256(ctx_ptr, true);
                };
            } else {
                trampoline = +[](void* ctx_ptr) {
                    auto* storage = static_cast<Callable*>(ctx_ptr);
                    (*storage)();
                    storage->~Callable();
                    detail::FreeTaskStorage256(ctx_ptr, false);
                };
            }

            Submit(Job{trampoline, ctx}, options);
        } else {
            SubmitFallbackTask(std::forward<F>(callable), options);
        }
    }

    void WaitIdle();
    void Shutdown(bool drain_pending = true);

    [[nodiscard]] std::size_t WorkerCount() const noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;

    [[nodiscard]] std::size_t WorkerFrameUsedBytes(std::size_t worker_index) const noexcept;
    [[nodiscard]] std::size_t WorkerGpuUsedBytes(std::size_t worker_index) const noexcept;

    [[nodiscard]] static std::optional<std::size_t> CurrentWorkerIndex() noexcept;

    [[nodiscard]] static FrameArena* CurrentWorkerFrameArena() noexcept;
    [[nodiscard]] static GPUArena*   CurrentWorkerGpuArena() noexcept;

    void IncrementFlushGeneration() noexcept;

    [[nodiscard]] static std::uint64_t GlobalFlushGeneration() noexcept;

    static void ResetAllWorkerFrameArenas() noexcept;

    void ResumeCoroutine(std::coroutine_handle<> handle, TaskOptions options = {});

    [[nodiscard]] ThreadPoolScheduler GetScheduler() noexcept;

private:
    template<typename F>
    void SubmitFallbackTask(F&& callable, TaskOptions options) {
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

    struct Impl;
    Impl* impl_;
};

} // namespace Engine
