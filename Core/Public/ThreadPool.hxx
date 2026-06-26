#pragma once

#include "CoreExport.hxx"

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

class FrameArena;
class GPUArena;

namespace Engine {

class ThreadPoolScheduler;

namespace detail {

std::size_t GetTaskPool32FreeCount() noexcept;
std::size_t GetTaskPool256FreeCount() noexcept;

} // namespace detail

struct TaskOptions {
    std::optional<std::size_t> target_worker{};
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

        if constexpr (kSize <= 256) {
            auto invoker = +[](void* ctx) {
                auto* c = static_cast<Callable*>(ctx);
                (*c)();
                c->~Callable();
            };
            auto constructor = +[](void* dst, void* src) {
                ::new (dst) Callable(std::move(*static_cast<Callable*>(src)));
            };
            SubmitInternal(&callable, invoker, constructor, kSize, options);
        } else {
            auto invoker = +[](void* ctx) {
                auto* c = static_cast<Callable*>(ctx);
                (*c)();
            };
            auto destructor = +[](void* ctx) {
                static_cast<Callable*>(ctx)->~Callable();
            };
            auto constructor = +[](void* dst, void* src) {
                ::new (dst) Callable(std::move(*static_cast<Callable*>(src)));
            };
            SubmitFallbackInternal(&callable, invoker, destructor, constructor, kSize, options);
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
    [[nodiscard]] static GPUArena*   CurrentWorkerGPUArena() noexcept;

    static void ResetAllWorkerArenas() noexcept;
    static void IncrementFlushGeneration() noexcept;

    [[nodiscard]] static std::uint64_t GlobalFlushGeneration() noexcept;

    void ResumeCoroutine(std::coroutine_handle<> handle, TaskOptions options = {});

    [[nodiscard]] ThreadPoolScheduler GetScheduler() noexcept;

private:
    void SubmitInternal(
        void* src_callable,
        void (*invoker)(void*),
        void (*constructor)(void*, void*),
        std::size_t lambda_size,
        TaskOptions options);

    void SubmitFallbackInternal(
        void* src_callable,
        void (*invoker)(void*),
        void (*destructor)(void*),
        void (*constructor)(void*, void*),
        std::size_t lambda_size,
        TaskOptions options);

    struct Impl;
    Impl* impl_;
};

} // namespace Engine
