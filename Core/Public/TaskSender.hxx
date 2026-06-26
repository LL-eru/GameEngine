#pragma once

#include "Execution.hxx"
#include "Senders.hxx"
#include "Task.hxx"
#include "ThreadPool.hxx"
#include "ThreadPoolScheduler.hxx"

#include <exception>
#include <type_traits>
#include <utility>

namespace Engine::exec {

template<typename T>
struct TaskSender {
    Task<T> task_{};
    ThreadPoolScheduler scheduler_{};

    using value_type = T;

    template<typename Receiver>
    struct Operation {
        Task<T> task_{};
        ThreadPoolScheduler scheduler_{};
        Receiver receiver_{};

        void start() {
            task_.Start();
            Pump();
        }

        void Pump() {
            if (task_.Done()) {
                Complete();
                return;
            }
            task_.ResumeOn(scheduler_);
            scheduler_.GetPool()->SubmitOnWorker(Job{&Operation::PumpJob, this});
        }

        static void PumpJob(void* ctx) { static_cast<Operation*>(ctx)->Pump(); }

        void Complete() {
            try {
                if constexpr (std::is_void_v<T>) {
                    task_.Result();
                    exec::set_value(std::move(receiver_));
                } else {
                    exec::set_value(std::move(receiver_), task_.Result());
                }
            } catch (...) {
                exec::set_error(std::move(receiver_), std::current_exception());
            }
        }
    };

    template<typename Receiver>
    [[nodiscard]] Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) const& {
        return Operation<std::decay_t<Receiver>>{
            task_,
            scheduler_,
            std::forward<Receiver>(receiver),
        };
    }

    template<typename Receiver>
    [[nodiscard]] Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) && {
        return Operation<std::decay_t<Receiver>>{
            std::move(task_),
            scheduler_,
            std::forward<Receiver>(receiver),
        };
    }
};

template<typename T>
struct sender_value_type<TaskSender<T>> {
    using type = T;
};

template<typename T>
[[nodiscard]] TaskSender<T> as_sender(Task<T>&& task, ThreadPoolScheduler scheduler) {
    return TaskSender<T>{std::move(task), scheduler};
}

} // namespace Engine::exec
