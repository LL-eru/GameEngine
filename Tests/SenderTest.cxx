// =============================================================================
// SenderTest.cxx - Phase 3 validation for Sender/Receiver execution API.
// =============================================================================

#include "Execution.hxx"
#include "Senders.hxx"
#include "Task.hxx"
#include "TaskSender.hxx"
#include "ThreadPool.hxx"
#include "ThreadPoolScheduler.hxx"

#include <atomic>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

std::atomic<int> g_failures{0};
std::atomic<int> g_checks{0};

void Report(bool ok, const char* expr, const char* file, int line) {
    g_checks.fetch_add(1, std::memory_order_relaxed);
    if (!ok) {
        g_failures.fetch_add(1, std::memory_order_relaxed);
        std::printf("  [FAIL] %s  (%s:%d)\n", expr, file, line);
    }
}

#define CHECK(cond) Report((cond), #cond, __FILE__, __LINE__)

void Section(const char* name) {
    std::printf("\n== %s ==\n", name);
    std::fflush(stdout);
}

struct ValueReceiver {
    int* out{};

    void set_value(int value) { *out = value; }
    void set_error(std::exception_ptr) {}
};

void TestConcepts(Engine::ThreadPoolScheduler scheduler) {
    Section("Test 3.1: Scheduler / Sender / Receiver concepts");
    static_assert(Engine::exec::Scheduler<Engine::ThreadPoolScheduler>);
    static_assert(Engine::exec::Receiver<ValueReceiver>);
    static_assert(Engine::exec::Sender<Engine::exec::ScheduleSender, ValueReceiver>);
    static_assert(Engine::exec::OperationState<
        decltype(std::declval<Engine::exec::ScheduleSender>().connect(std::declval<ValueReceiver>()))>);
    CHECK(true);
}

void TestSchedule(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("Test 3.2a: schedule completes on a pool worker");
    const auto worker = Engine::exec::sync_wait(Engine::exec::then(
        Engine::exec::schedule(scheduler),
        []() -> std::optional<std::size_t> { return Engine::ThreadPool::CurrentWorkerIndex(); }));
    CHECK(worker.has_value());
    pool.WaitIdle();
}

void TestThenChain(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("Test 3.2b: then chains sender results");
    const int value = Engine::exec::sync_wait(Engine::exec::then(
        Engine::exec::just(21),
        [](int input) { return input * 2; }));
    CHECK(value == 42);
    pool.WaitIdle();
}

void TestTransfer(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("Test 3.2c: transfer delivers completion on scheduler worker");
    const int value = Engine::exec::sync_wait(Engine::exec::transfer(Engine::exec::just(5), scheduler));
    CHECK(value == 5);
    pool.WaitIdle();
}

void TestPipeline(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("Test 3.2d: schedule -> then -> transfer pipeline");
    const int value = Engine::exec::sync_wait(Engine::exec::then(
        Engine::exec::transfer(
            Engine::exec::then(
                Engine::exec::schedule(scheduler),
                []() { return 10; }),
            scheduler),
        [](int input) { return input + 5; }));
    CHECK(value == 15);
    pool.WaitIdle();
}

Engine::Task<int> MakeTaskValue(Engine::ThreadPoolScheduler scheduler, int value) {
    co_await Engine::switch_to(scheduler);
    co_return value;
}

void TestTaskAsSender(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("Test 3.3: Task<T> adapts to Sender via as_sender");
    auto task = MakeTaskValue(scheduler, 77);
    const int value = Engine::exec::sync_wait(Engine::exec::as_sender(std::move(task), scheduler));
    CHECK(value == 77);
    pool.WaitIdle();
}

void TestErrorPropagation(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("Test 3.4: then propagates errors to sync_wait");
    bool caught = false;
    try {
        (void)Engine::exec::sync_wait(Engine::exec::then(
            Engine::exec::just(1),
            [](int) -> int { throw std::runtime_error("sender failure"); }));
    } catch (const std::runtime_error& ex) {
        caught = std::string_view{ex.what()} == "sender failure";
    }
    CHECK(caught);
    pool.WaitIdle();
}

} // namespace

int main() {
    std::printf("Sender/Receiver (Phase 3) test suite\n");
    std::fflush(stdout);

    Engine::ThreadPool pool(4);
    const Engine::ThreadPoolScheduler scheduler = pool.GetScheduler();

    TestConcepts(scheduler);
    TestSchedule(pool, scheduler);
    TestThenChain(pool, scheduler);
    TestTransfer(pool, scheduler);
    TestPipeline(pool, scheduler);
    TestTaskAsSender(pool, scheduler);
    TestErrorPropagation(pool, scheduler);

    pool.Shutdown();

    const int checks = g_checks.load();
    const int failures = g_failures.load();
    std::printf("\n----------------------------------------\n");
    std::printf("Checks run : %d\n", checks);
    std::printf("Failures   : %d\n", failures);
    std::printf("Result     : %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
