// =============================================================================
// TaskTest.cxx - Phase 2 validation for coroutine Task<T> integration.
// =============================================================================

#include "Task.hxx"
#include "ThreadPool.hxx"
#include "ThreadPoolScheduler.hxx"
#include "CoroutineFrameAllocator.hxx"

#include <atomic>
#include <cstdio>
#include <expected>
#include <string_view>
#include <vector>

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

struct NeverResumeAwaiter {
    [[nodiscard]] bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
};

template<typename T>
T RunTaskSync(Engine::ThreadPool& pool, Engine::Task<T> task) {
    task.Start();
    pool.WaitIdle();
    return task.Result();
}

void RunTaskSync(Engine::ThreadPool& pool, Engine::Task<void> task) {
    task.Start();
    pool.WaitIdle();
    task.Result();
}

Engine::Task<int> MakeSwitchReturnTask(Engine::ThreadPoolScheduler scheduler, int value) {
    co_await Engine::switch_to(scheduler);
    co_return value;
}

Engine::Task<void> MakePinnedWorkerTask(
    Engine::ThreadPoolScheduler scheduler,
    std::size_t expected_worker,
    std::atomic<int>* violations) {
    co_await Engine::switch_to(
        scheduler,
        Engine::TaskOptions{.target_worker = expected_worker, .pinned = true});
    const auto worker = Engine::ThreadPool::CurrentWorkerIndex();
    if (!worker.has_value() || *worker != expected_worker) {
        violations->fetch_add(1, std::memory_order_relaxed);
    }
}

enum class TestError { Failed };

Engine::Task<std::expected<int, TestError>> MakeExpectedFailureTask(Engine::ThreadPoolScheduler scheduler) {
    co_await Engine::switch_to(scheduler);
    co_return std::unexpected(TestError::Failed);
}

Engine::Task<void> ConsumeExpectedFailureTask(
    Engine::ThreadPoolScheduler scheduler,
    std::atomic<bool>* observed) {
    const auto result = co_await MakeExpectedFailureTask(scheduler);
    if (!result.has_value() && result.error() == TestError::Failed) {
        observed->store(true, std::memory_order_release);
    }
}

Engine::Task<void> MakeSuspendedTask(Engine::ThreadPoolScheduler scheduler) {
    co_await Engine::switch_to(scheduler);
    co_await NeverResumeAwaiter{};
}

Engine::Task<int> MakeNestedTask(Engine::ThreadPoolScheduler scheduler) {
    co_await Engine::switch_to(scheduler);

    auto inner = []() -> Engine::Task<int> {
        co_return 21;
    }();

    const int inner_value = co_await std::move(inner);
    co_return inner_value * 2;
}

void TestBasicTaskValue(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("Test 2.1: basic Task<int> on thread pool");
    const int value = RunTaskSync(pool, MakeSwitchReturnTask(scheduler, 42));
    CHECK(value == 42);
}

void TestNestedAwait(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("Test 2.2: nested co_await Task<T>");
    const int value = RunTaskSync(pool, MakeNestedTask(scheduler));
    CHECK(value == 42);
}

void TestSwitchToWorkerThread(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("Test 2.3: switch_to resumes on a pool worker");

    auto probe = [&pool, scheduler]() -> Engine::Task<bool> {
        co_await Engine::switch_to(scheduler);
        co_return Engine::ThreadPool::CurrentWorkerIndex().has_value();
    };

    const bool on_worker = RunTaskSync(pool, probe());
    CHECK(on_worker);
}

void TestConcurrentCoroutines(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("Test 2.4: many coroutines complete without loss");
    std::atomic<int> counter{0};

    auto increment = [&counter, scheduler]() -> Engine::Task<void> {
        co_await Engine::switch_to(scheduler);
        counter.fetch_add(1, std::memory_order_relaxed);
    };

    constexpr int kTasks = 200;
    std::vector<Engine::Task<void>> tasks;
    tasks.reserve(kTasks);
    for (int i = 0; i < kTasks; ++i) {
        tasks.push_back(increment());
    }
    for (auto& task : tasks) {
        task.Start();
    }
    pool.WaitIdle();
    CHECK(counter.load() == kTasks);
}

void TestExpectedErrorPropagation(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("Test 2.5: std::expected error propagation via co_await");
    std::atomic<bool> observed{false};
    RunTaskSync(pool, ConsumeExpectedFailureTask(scheduler, &observed));
    CHECK(observed.load(std::memory_order_acquire));
}

void TestPinnedCoroutineDispatch(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("Test 2.6: pinned coroutine dispatch to target worker");
    std::atomic<int> violations{0};
    constexpr int kTasksPerWorker = 20;
    const std::size_t worker_count = pool.WorkerCount();

    std::vector<Engine::Task<void>> tasks;
    tasks.reserve(worker_count * kTasksPerWorker);
    for (std::size_t target = 0; target < worker_count; ++target) {
        for (int i = 0; i < kTasksPerWorker; ++i) {
            tasks.push_back(MakePinnedWorkerTask(scheduler, target, &violations));
        }
    }
    for (auto& task : tasks) {
        task.Start();
    }
    pool.WaitIdle();
    CHECK(violations.load() == 0);
}

void TestDestroySuspendedTask(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("Test 2.7: destroying suspended Task frees coroutine frame");
    const std::size_t baseline = Engine::detail::CoroutineFrameAllocator::LiveAllocations();

    {
        auto task = MakeSuspendedTask(scheduler);
        task.Start();
        pool.WaitIdle();
        CHECK(Engine::detail::CoroutineFrameAllocator::LiveAllocations() > baseline);
    }

    CHECK(Engine::detail::CoroutineFrameAllocator::LiveAllocations() == baseline);
}

void TestSteadyStateNoAllocGrowth(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("Test 2.8: steady-state coroutine loop does not grow frame allocations");

    auto run_once = [&scheduler]() -> Engine::Task<int> {
        co_await Engine::switch_to(scheduler);
        co_return 1;
    };

    for (int i = 0; i < 64; ++i) {
        RunTaskSync(pool, run_once());
    }

    const std::size_t baseline_live = Engine::detail::CoroutineFrameAllocator::LiveAllocations();
    const std::size_t baseline_bytes = Engine::detail::CoroutineFrameAllocator::TotalBytesAllocated();

    for (int i = 0; i < 512; ++i) {
        RunTaskSync(pool, run_once());
    }

    CHECK(Engine::detail::CoroutineFrameAllocator::LiveAllocations() == baseline_live);
    CHECK(Engine::detail::CoroutineFrameAllocator::TotalBytesAllocated() == baseline_bytes);
}

void TestExceptionPropagation(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("Test 2.9: exception propagation through co_await");

    auto throws = [scheduler]() -> Engine::Task<int> {
        co_await Engine::switch_to(scheduler);
        throw std::runtime_error("boom");
        co_return 0;
    };

    auto consumer = [&throws]() -> Engine::Task<bool> {
        try {
            (void)co_await throws();
            co_return false;
        } catch (const std::runtime_error& ex) {
            co_return std::string_view{ex.what()} == "boom";
        }
    };

    const bool caught = RunTaskSync(pool, consumer());
    CHECK(caught);
}

} // namespace

int main() {
    std::printf("Task (Phase 2) test suite\n");
    std::fflush(stdout);

    Engine::ThreadPool pool(4);
    const Engine::ThreadPoolScheduler scheduler = pool.GetScheduler();

    TestBasicTaskValue(pool, scheduler);
    TestNestedAwait(pool, scheduler);
    TestSwitchToWorkerThread(pool, scheduler);
    TestConcurrentCoroutines(pool, scheduler);
    TestExpectedErrorPropagation(pool, scheduler);
    TestPinnedCoroutineDispatch(pool, scheduler);
    TestDestroySuspendedTask(pool, scheduler);
    TestSteadyStateNoAllocGrowth(pool, scheduler);
    TestExceptionPropagation(pool, scheduler);

    pool.Shutdown();

    const int checks = g_checks.load();
    const int failures = g_failures.load();
    std::printf("\n----------------------------------------\n");
    std::printf("Checks run : %d\n", checks);
    std::printf("Failures   : %d\n", failures);
    std::printf("Result     : %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
