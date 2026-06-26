#pragma once

#include <concepts>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

namespace Engine::exec {

struct set_value_t {
    template<typename R>
    void operator()(R&& receiver) const
        requires requires { std::forward<R>(receiver).set_value(); }
    {
        std::forward<R>(receiver).set_value();
    }

    template<typename R, typename V>
    void operator()(R&& receiver, V&& value) const
        requires requires { std::forward<R>(receiver).set_value(std::forward<V>(value)); }
    {
        std::forward<R>(receiver).set_value(std::forward<V>(value));
    }
};

inline constexpr set_value_t set_value{};

struct set_error_t {
    template<typename R>
    void operator()(R&& receiver, std::exception_ptr error) const
        requires requires { std::forward<R>(receiver).set_error(error); }
    {
        std::forward<R>(receiver).set_error(error);
    }
};

inline constexpr set_error_t set_error{};

template<typename R>
concept Receiver =
    requires(R&& receiver) { set_value(std::forward<R>(receiver)); } ||
    requires(R&& receiver, int value) { set_value(std::forward<R>(receiver), value); };

template<typename Op>
concept OperationState = requires(Op& op) { { op.start() } -> std::same_as<void>; };

template<typename S, typename R>
    requires requires(S&& sender, R&& receiver) {
        { std::forward<S>(sender).connect(std::forward<R>(receiver)) } -> OperationState;
    }
auto connect(S&& sender, R&& receiver) {
    return std::forward<S>(sender).connect(std::forward<R>(receiver));
}

template<typename S, typename R>
concept Sender = requires(S&& sender, R&& receiver) {
    { connect(std::forward<S>(sender), std::forward<R>(receiver)) } -> OperationState;
};

template<typename T>
struct sender_value_type {
    using type = T;
};

template<typename T>
using sender_value_type_t = typename sender_value_type<T>::type;

namespace detail {

template<typename T>
struct SyncWaitState {
    std::mutex mutex{};
    std::condition_variable cv{};
    bool done = false;
    std::exception_ptr error{};
    std::optional<T> value{};
};

template<>
struct SyncWaitState<void> {
    std::mutex mutex{};
    std::condition_variable cv{};
    bool done = false;
    std::exception_ptr error{};
};

template<typename T>
void MarkDone(SyncWaitState<T>& state) {
    {
        std::lock_guard lock(state.mutex);
        state.done = true;
    }
    state.cv.notify_one();
}

template<typename T>
void WaitForDone(SyncWaitState<T>& state) {
    std::unique_lock lock(state.mutex);
    state.cv.wait(lock, [&state] { return state.done; });
}

template<typename T>
struct SyncWaitReceiver {
    SyncWaitState<T>* state{};

    void set_value(T value) {
        {
            std::lock_guard lock(state->mutex);
            state->value = std::move(value);
            state->done = true;
        }
        state->cv.notify_one();
    }

    void set_error(std::exception_ptr error) {
        {
            std::lock_guard lock(state->mutex);
            state->error = error;
            state->done = true;
        }
        state->cv.notify_one();
    }
};

template<>
struct SyncWaitReceiver<void> {
    SyncWaitState<void>* state{};

    void set_value() { MarkDone(*state); }

    void set_error(std::exception_ptr error) {
        {
            std::lock_guard lock(state->mutex);
            state->error = error;
            state->done = true;
        }
        state->cv.notify_one();
    }
};

} // namespace detail

template<typename Sender>
    requires(!std::same_as<sender_value_type_t<Sender>, void>)
auto sync_wait(Sender&& sender) {
    using Value = sender_value_type_t<Sender>;
    detail::SyncWaitState<Value> state{};
    detail::SyncWaitReceiver<Value> receiver{&state};
    auto operation = connect(std::forward<Sender>(sender), receiver);
    operation.start();
    detail::WaitForDone(state);
    if (state.error) {
        std::rethrow_exception(state.error);
    }
    return std::move(*state.value);
}

template<typename Sender>
    requires std::same_as<sender_value_type_t<Sender>, void>
void sync_wait(Sender&& sender) {
    detail::SyncWaitState<void> state{};
    detail::SyncWaitReceiver<void> receiver{&state};
    auto operation = connect(std::forward<Sender>(sender), receiver);
    operation.start();
    detail::WaitForDone(state);
    if (state.error) {
        std::rethrow_exception(state.error);
    }
}

} // namespace Engine::exec
