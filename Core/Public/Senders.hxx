#pragma once

#include "Execution.hxx"
#include "ThreadPool.hxx"
#include "ThreadPoolScheduler.hxx"

#include <exception>
#include <type_traits>
#include <utility>

namespace Engine::exec {

struct ScheduleSender {
    ThreadPoolScheduler scheduler_;

    using value_type = void;

    template<typename Receiver>
    struct Operation {
        ThreadPoolScheduler scheduler_{};
        Receiver receiver_{};

        void start() {
            struct Context {
                Receiver receiver;
            };
            auto* context = new Context{std::move(receiver_)};
            scheduler_.GetPool()->SubmitOnWorker(
                Job{
                    [](void* raw) {
                        auto* ctx = static_cast<Context*>(raw);
                        exec::set_value(std::move(ctx->receiver));
                        delete ctx;
                    },
                    context,
                });
        }
    };

    template<typename Receiver>
    [[nodiscard]] Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) const& {
        return Operation<std::decay_t<Receiver>>{scheduler_, std::forward<Receiver>(receiver)};
    }

    template<typename Receiver>
    [[nodiscard]] Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) && {
        return Operation<std::decay_t<Receiver>>{scheduler_, std::forward<Receiver>(receiver)};
    }
};

template<>
struct sender_value_type<ScheduleSender> {
    using type = void;
};

[[nodiscard]] inline ScheduleSender schedule(ThreadPoolScheduler scheduler) noexcept {
    return ScheduleSender{scheduler};
}

template<typename T>
struct JustSender {
    T value_{};

    using value_type = T;

    template<typename Receiver>
    struct Operation {
        T value_{};
        Receiver receiver_{};

        void start() { exec::set_value(std::move(receiver_), std::move(value_)); }
    };

    template<typename Receiver>
    [[nodiscard]] Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) const& {
        return Operation<std::decay_t<Receiver>>{value_, std::forward<Receiver>(receiver)};
    }

    template<typename Receiver>
    [[nodiscard]] Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) && {
        return Operation<std::decay_t<Receiver>>{std::move(value_), std::forward<Receiver>(receiver)};
    }
};

template<>
struct JustSender<void> {
    using value_type = void;

    template<typename Receiver>
    struct Operation {
        Receiver receiver_{};

        void start() { exec::set_value(std::move(receiver_)); }
    };

    template<typename Receiver>
    [[nodiscard]] Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) const& {
        return Operation<std::decay_t<Receiver>>{std::forward<Receiver>(receiver)};
    }

    template<typename Receiver>
    [[nodiscard]] Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) && {
        return Operation<std::decay_t<Receiver>>{std::forward<Receiver>(receiver)};
    }
};

template<typename T>
struct sender_value_type<JustSender<T>> {
    using type = T;
};

template<typename T>
[[nodiscard]] JustSender<T> just(T value) {
    return JustSender<T>{std::move(value)};
}

[[nodiscard]] inline JustSender<void> just() { return JustSender<void>{}; }

template<typename Sender>
struct TransferSender {
    Sender sender_{};
    ThreadPoolScheduler scheduler_{};

    using value_type = sender_value_type_t<Sender>;

    template<typename Receiver>
    struct Operation {
        Sender sender_{};
        ThreadPoolScheduler scheduler_{};
        Receiver receiver_{};

        struct InnerReceiver {
            ThreadPoolScheduler scheduler_{};
            Receiver receiver_{};

            void set_value() requires std::same_as<value_type, void> {
                struct Context {
                    Receiver receiver;
                };
                auto* context = new Context{std::move(receiver_)};
                scheduler_.GetPool()->SubmitOnWorker(
                    Job{
                        [](void* raw) {
                            auto* ctx = static_cast<Context*>(raw);
                            exec::set_value(std::move(ctx->receiver));
                            delete ctx;
                        },
                        context,
                    });
            }

            template<typename V>
            void set_value(V&& value) requires(!std::same_as<value_type, void>) {
                struct Context {
                    Receiver receiver;
                    value_type value;
                };
                auto* context = new Context{std::move(receiver_), std::forward<V>(value)};
                scheduler_.GetPool()->SubmitOnWorker(
                    Job{
                        [](void* raw) {
                            auto* ctx = static_cast<Context*>(raw);
                            exec::set_value(std::move(ctx->receiver), std::move(ctx->value));
                            delete ctx;
                        },
                        context,
                    });
            }

            void set_error(std::exception_ptr error) { exec::set_error(receiver_, error); }
        };

        void start() {
            exec::connect(
                std::move(sender_),
                InnerReceiver{scheduler_, std::move(receiver_)})
                .start();
        }
    };

    template<typename Receiver>
    [[nodiscard]] Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) const& {
        return Operation<std::decay_t<Receiver>>{sender_, scheduler_, std::forward<Receiver>(receiver)};
    }

    template<typename Receiver>
    [[nodiscard]] Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) && {
        return Operation<std::decay_t<Receiver>>{
            std::move(sender_),
            scheduler_,
            std::forward<Receiver>(receiver),
        };
    }
};

template<typename Sender>
struct sender_value_type<TransferSender<Sender>> {
    using type = sender_value_type_t<Sender>;
};

template<typename Sender>
[[nodiscard]] TransferSender<std::decay_t<Sender>> transfer(
    Sender&& sender,
    ThreadPoolScheduler scheduler) {
    return TransferSender<std::decay_t<Sender>>{std::forward<Sender>(sender), scheduler};
}

namespace detail {

template<typename Fn, typename Input>
struct ThenResult;

template<typename Fn>
struct ThenResult<Fn, void> {
    using type = std::invoke_result_t<Fn>;
};

template<typename Fn, typename Input>
    requires(!std::is_void_v<Input>)
struct ThenResult<Fn, Input> {
    using type = std::invoke_result_t<Fn, Input>;
};

} // namespace detail

template<typename Sender, typename Fn>
struct ThenSender {
    Sender sender_{};
    Fn fn_{};

    using value_type = detail::ThenResult<Fn, sender_value_type_t<Sender>>::type;

    template<typename Receiver>
    struct Operation {
        Sender sender_{};
        Fn fn_{};
        Receiver receiver_{};

        struct InnerReceiver {
            Fn fn_{};
            Receiver receiver_{};

            void set_value() requires std::is_void_v<sender_value_type_t<Sender>> {
                try {
                    if constexpr (std::is_void_v<value_type>) {
                        fn_();
                        exec::set_value(std::move(receiver_));
                    } else {
                        exec::set_value(std::move(receiver_), fn_());
                    }
                } catch (...) {
                    exec::set_error(std::move(receiver_), std::current_exception());
                }
            }

            template<typename V>
            void set_value(V&& value)
                requires(!std::is_void_v<sender_value_type_t<Sender>>) {
                try {
                    if constexpr (std::is_void_v<value_type>) {
                        fn_(std::forward<V>(value));
                        exec::set_value(std::move(receiver_));
                    } else {
                        exec::set_value(std::move(receiver_), fn_(std::forward<V>(value)));
                    }
                } catch (...) {
                    exec::set_error(std::move(receiver_), std::current_exception());
                }
            }

            void set_error(std::exception_ptr error) { exec::set_error(receiver_, error); }
        };

        void start() {
            exec::connect(
                std::move(sender_),
                InnerReceiver{std::move(fn_), std::move(receiver_)})
                .start();
        }
    };

    template<typename Receiver>
    [[nodiscard]] Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) const& {
        return Operation<std::decay_t<Receiver>>{sender_, fn_, std::forward<Receiver>(receiver)};
    }

    template<typename Receiver>
    [[nodiscard]] Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) && {
        return Operation<std::decay_t<Receiver>>{
            std::move(sender_),
            std::move(fn_),
            std::forward<Receiver>(receiver),
        };
    }
};

template<typename Sender, typename Fn>
struct sender_value_type<ThenSender<Sender, Fn>> {
    using type = typename ThenSender<Sender, Fn>::value_type;
};

template<typename Sender, typename Fn>
[[nodiscard]] ThenSender<std::decay_t<Sender>, std::decay_t<Fn>> then(
    Sender&& sender,
    Fn&& fn) {
    return ThenSender<std::decay_t<Sender>, std::decay_t<Fn>>{
        std::forward<Sender>(sender),
        std::forward<Fn>(fn),
    };
}

template<typename S, typename R>
concept SchedulerFor = requires(S&& scheduler, R&& /*unused*/) {
    { schedule(std::forward<S>(scheduler)) };
};

template<typename S>
concept Scheduler = SchedulerFor<S, int>;

} // namespace Engine::exec
