#pragma once

#include "CoroutineFrameAllocator.hxx"
#include "ThreadPoolScheduler.hxx"

#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Engine {

template<typename T>
class Task;

namespace detail {

template<typename T>
struct TaskPromiseBase {
    std::exception_ptr exception{};
    std::coroutine_handle<> continuation{};

    struct FinalAwaiter {
        std::coroutine_handle<> continuation{};

        [[nodiscard]] bool await_ready() const noexcept { return false; }

        [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept {
            return continuation ? continuation : std::noop_coroutine();
        }

        void await_resume() const noexcept {}
    };

    [[nodiscard]] FinalAwaiter final_suspend() noexcept { return FinalAwaiter{continuation}; }

    void unhandled_exception() { exception = std::current_exception(); }

    static void* operator new(std::size_t size) { return CoroutineFrameAllocator::Allocate(size); }

    static void operator delete(void* ptr, std::size_t size) noexcept {
        CoroutineFrameAllocator::Deallocate(ptr, size);
    }
};

template<typename T>
struct TaskAwaiter {
    std::coroutine_handle<typename Task<T>::promise_type> handle{};

    [[nodiscard]] bool await_ready() const noexcept { return handle.done(); }

    void await_suspend(std::coroutine_handle<> continuation) const {
        handle.promise().continuation = continuation;
        handle.resume();
    }

    auto await_resume() -> std::conditional_t<std::is_void_v<T>, void, T> {
        auto& promise = handle.promise();
        if (promise.exception) {
            std::rethrow_exception(promise.exception);
        }
        if constexpr (!std::is_void_v<T>) {
            return std::move(*promise.value);
        }
    }
};

} // namespace detail

template<typename T>
class Task {
public:
    struct promise_type : detail::TaskPromiseBase<T> {
        std::optional<T> value{};

        [[nodiscard]] Task get_return_object() noexcept {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        [[nodiscard]] std::suspend_always initial_suspend() noexcept { return {}; }

        void return_value(T v) { value = std::move(v); }
    };

    Task() noexcept = default;

    explicit Task(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {}

    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            DestroyHandle();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task() { DestroyHandle(); }

    [[nodiscard]] bool Valid() const noexcept { return static_cast<bool>(handle_); }

    [[nodiscard]] detail::TaskAwaiter<T> operator co_await() const& = delete;

    [[nodiscard]] detail::TaskAwaiter<T> operator co_await() && noexcept {
        return detail::TaskAwaiter<T>{std::exchange(handle_, {})};
    }

    void Start() {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    [[nodiscard]] bool Done() const noexcept { return !handle_ || handle_.done(); }

    void ResumeOn(ThreadPoolScheduler scheduler) {
        if (handle_ && !handle_.done()) {
            scheduler.GetPool()->ResumeCoroutine(
                std::coroutine_handle<>::from_address(handle_.address()));
        }
    }

    T Result() {
        if (!handle_) {
            throw std::runtime_error("Task has no result");
        }
        if (!handle_.done()) {
            throw std::runtime_error("Task is not complete");
        }
        auto& promise = handle_.promise();
        if (promise.exception) {
            std::rethrow_exception(promise.exception);
        }
        return std::move(*promise.value);
    }

private:
    void DestroyHandle() noexcept {
        if (handle_) {
            handle_.destroy();
            handle_ = {};
        }
    }

    std::coroutine_handle<promise_type> handle_{};
};

template<>
class Task<void> {
public:
    struct promise_type : detail::TaskPromiseBase<void> {
        [[nodiscard]] Task get_return_object() noexcept {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        [[nodiscard]] std::suspend_always initial_suspend() noexcept { return {}; }

        void return_void() noexcept {}
    };

    Task() noexcept = default;

    explicit Task(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {}

    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            DestroyHandle();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task() { DestroyHandle(); }

    [[nodiscard]] bool Valid() const noexcept { return static_cast<bool>(handle_); }

    [[nodiscard]] detail::TaskAwaiter<void> operator co_await() const& = delete;

    [[nodiscard]] detail::TaskAwaiter<void> operator co_await() && noexcept {
        return detail::TaskAwaiter<void>{std::exchange(handle_, {})};
    }

    void Start() {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    [[nodiscard]] bool Done() const noexcept { return !handle_ || handle_.done(); }

    void ResumeOn(ThreadPoolScheduler scheduler) {
        if (handle_ && !handle_.done()) {
            scheduler.GetPool()->ResumeCoroutine(
                std::coroutine_handle<>::from_address(handle_.address()));
        }
    }

    void Result() {
        if (!handle_) {
            return;
        }
        if (!handle_.done()) {
            throw std::runtime_error("Task is not complete");
        }
        auto& promise = handle_.promise();
        if (promise.exception) {
            std::rethrow_exception(promise.exception);
        }
    }

private:
    void DestroyHandle() noexcept {
        if (handle_) {
            handle_.destroy();
            handle_ = {};
        }
    }

    std::coroutine_handle<promise_type> handle_{};
};

} // namespace Engine
