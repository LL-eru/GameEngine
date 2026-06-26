// =============================================================================
// ThreadPoolTest.cxx ? Phase 1 validation for the work-stealing thread pool.
// =============================================================================

#include "ThreadPool.hxx"
#include "WorkStealingDeque.hxx"

#include <atomic>
#include <cstdio>
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

static void IncAtomic(void* ctx) {
    static_cast<std::atomic<int>*>(ctx)->fetch_add(1, std::memory_order_relaxed);
}

struct WorkerCountContext {
    std::vector<std::atomic<int>>* counts;
};

static void RecordWorkerTask(void* ctx) {
    auto* data = static_cast<WorkerCountContext*>(ctx);
    const auto worker = Engine::ThreadPool::CurrentWorkerIndex();
    if (worker.has_value()) {
        (*data->counts)[*worker].fetch_add(1, std::memory_order_relaxed);
    }
}

struct SeedContext {
    Engine::ThreadPool* pool;
    WorkerCountContext* count_ctx;
    int task_count;
    std::atomic<bool>* done;
};

static void SeedSkewedTasks(void* ctx) {
    auto* seed = static_cast<SeedContext*>(ctx);
    for (int i = 0; i < seed->task_count; ++i) {
        seed->pool->Submit(
            Engine::Job{RecordWorkerTask, seed->count_ctx},
            Engine::TaskOptions{.target_worker = 0, .pinned = false});
    }
    seed->done->store(true, std::memory_order_release);
}

struct PinContext {
    std::size_t expected_worker;
    std::atomic<int>* violations;
};

static void CheckPinnedWorker(void* ctx) {
    auto* pin = static_cast<PinContext*>(ctx);
    const auto worker = Engine::ThreadPool::CurrentWorkerIndex();
    if (!worker.has_value() || *worker != pin->expected_worker) {
        pin->violations->fetch_add(1, std::memory_order_relaxed);
    }
}

void TestDequeUnit() {
    Section("Test 1.6: WorkStealingDeque single-thread push/pop");
    Engine::WorkStealingDeque<int, 8> deque;
    for (int i = 0; i < 8; ++i) {
        CHECK(deque.Push(i));
    }
    CHECK(!deque.Push(99));

    for (int i = 7; i >= 0; --i) {
        const auto value = deque.Pop();
        CHECK(value.has_value());
        CHECK(*value == i);
    }
    CHECK(!deque.Pop().has_value());
}

void TestBasicTasks(Engine::ThreadPool& pool, const char* label, int task_count) {
    Section(label);
    std::atomic<int> counter{0};
    for (int i = 0; i < task_count; ++i) {
        pool.Submit(Engine::Job{IncAtomic, &counter});
    }
    pool.WaitIdle();
    CHECK(counter.load() == task_count);
}

void TestDependencyOrder(Engine::ThreadPool& pool) {
    Section("Test 1.3: FIFO submits to worker 0");
    std::atomic<int> counter{0};
    for (int i = 0; i < 3; ++i) {
        pool.Submit(
            Engine::Job{IncAtomic, &counter},
            Engine::TaskOptions{.target_worker = 0, .pinned = false});
    }
    pool.WaitIdle();
    CHECK(counter.load() == 3);
}

void TestWorkStealing(Engine::ThreadPool& pool, std::size_t worker_count) {
    Section("Test 1.4: work-stealing under skewed load");
    std::vector<std::atomic<int>> executed_by_worker(worker_count);
    for (auto& counter : executed_by_worker) {
        counter.store(0, std::memory_order_relaxed);
    }

    constexpr int kTasks = 200;
    WorkerCountContext count_ctx{&executed_by_worker};
    std::atomic<bool> seed_done{false};
    SeedContext seed{&pool, &count_ctx, kTasks, &seed_done};

    pool.Submit(
        Engine::Job{SeedSkewedTasks, &seed},
        Engine::TaskOptions{.target_worker = 0, .pinned = true});

    pool.WaitIdle();
    CHECK(seed_done.load(std::memory_order_acquire));

    int total = 0;
    for (std::size_t w = 0; w < worker_count; ++w) {
        total += executed_by_worker[w].load();
        std::printf("  worker %zu executed: %d\n", w, executed_by_worker[w].load());
    }
    CHECK(total == kTasks);
    CHECK(executed_by_worker[0].load() < kTasks);

    int workers_with_work = 0;
    for (std::size_t w = 0; w < worker_count; ++w) {
        if (executed_by_worker[w].load() > 0) {
            ++workers_with_work;
        }
    }
    CHECK(workers_with_work >= 2);
}

void TestPinning(Engine::ThreadPool& pool, std::size_t worker_count) {
    Section("Test 1.5: pinned tasks stay on target worker");
    std::vector<std::atomic<int>> violations(worker_count);
    for (auto& v : violations) {
        v.store(0, std::memory_order_relaxed);
    }

    constexpr int kTasksPerWorker = 50;
    std::vector<PinContext> contexts(worker_count * kTasksPerWorker);
    std::size_t context_index = 0;
    for (std::size_t target = 0; target < worker_count; ++target) {
        for (int i = 0; i < kTasksPerWorker; ++i) {
            contexts[context_index] = PinContext{target, &violations[target]};
            pool.Submit(
                Engine::Job{CheckPinnedWorker, &contexts[context_index]},
                Engine::TaskOptions{.target_worker = target, .pinned = true});
            ++context_index;
        }
    }

    pool.WaitIdle();

    for (std::size_t target = 0; target < worker_count; ++target) {
        CHECK(violations[target].load() == 0);
    }
}

} // namespace

int main() {
    std::printf("Thread pool (Phase 1) test suite\n");
    std::fflush(stdout);

    TestDequeUnit();

    Engine::ThreadPool pool(4);
    TestBasicTasks(pool, "Test 1.1: basic tasks", 100);
    TestBasicTasks(pool, "Test 1.2: basic tasks (continued load)", 100);
    TestDependencyOrder(pool);
    pool.Shutdown();

    const int checks = g_checks.load();
    const int failures = g_failures.load();
    std::printf("\n----------------------------------------\n");
    std::printf("Checks run : %d\n", checks);
    std::printf("Failures   : %d\n", failures);
    std::printf("Result     : %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
