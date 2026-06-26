// =============================================================================
// TaskMemoryTest.cxx - Task/memory integration validation (UT / IT / PT).
// =============================================================================

#include "EngineAllocator.hxx"
#include "MemoryAPI.hxx"
#include "Task.hxx"
#include "TestDiagnostics.hxx"
#include "ThreadPool.hxx"
#include "ThreadPoolScheduler.hxx"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
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

constexpr std::size_t kTaskPool32Capacity = 1024;
constexpr std::size_t kTaskPool256Capacity = 128;

// -----------------------------------------------------------------------------
// UT-1: Size-tier dispatch and pool fallback
// -----------------------------------------------------------------------------

void TestSmallTaskPoolRoundTrip(Engine::ThreadPool& pool) {
    Section("UT-1.1: small tasks (<=32B) use 32B pool and return slots");

    const std::size_t baseline_free = Engine::detail::GetTaskPool32FreeCount();
    CHECK(baseline_free == 0 || baseline_free <= kTaskPool32Capacity);

    std::atomic<int> counter{0};
    for (int i = 0; i < 1000; ++i) {
        pool.Submit([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    pool.WaitIdle();
    CHECK(counter.load() == 1000);

    const std::size_t after_free = Engine::detail::GetTaskPool32FreeCount();
    CHECK(after_free == kTaskPool32Capacity);
}

struct LargeCapture128 {
    std::array<std::uint8_t, 124> payload{};
    int marker = 0xBEEF;
};

void TestLargeTaskPoolRoundTrip(Engine::ThreadPool& pool) {
    Section("UT-1.2: 128B capture uses 256B pool");

    static_assert(sizeof(LargeCapture128) <= 256);
    static_assert(sizeof(LargeCapture128) > 32);

    std::atomic<bool> executed{false};
    const LargeCapture128 seed{};
    pool.Submit([ capture = seed, &executed ]() {
        executed.store(capture.marker == 0xBEEF, std::memory_order_release);
    });
    pool.WaitIdle();
    CHECK(executed.load(std::memory_order_acquire));

    const std::size_t after_free = Engine::detail::GetTaskPool256FreeCount();
    CHECK(after_free == kTaskPool256Capacity);
}

void TestPoolExhaustionFallback(Engine::ThreadPool& pool) {
    Section("UT-1.3: pool exhaustion falls back to heap without crash");

    std::atomic<int> counter{0};
    constexpr int kTasks = static_cast<int>(kTaskPool32Capacity) + 512;

    for (int i = 0; i < kTasks; ++i) {
        pool.Submit([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    pool.WaitIdle();
    CHECK(counter.load() == kTasks);

    const std::size_t after_free = Engine::detail::GetTaskPool32FreeCount();
    CHECK(after_free == kTaskPool32Capacity);
}

struct HugeCapture512 {
    std::array<std::uint8_t, 512> payload{};
};

void TestFallbackForOversizedCapture(Engine::ThreadPool& pool) {
    Section("UT-1.4: >256B capture uses heap fallback path");

    static_assert(sizeof(HugeCapture512) > 256);

    std::atomic<bool> executed{false};
    const HugeCapture512 seed{};
    pool.Submit([ capture = seed, &executed ]() {
        executed.store(capture.payload.back() == 0, std::memory_order_release);
    });
    pool.WaitIdle();
    CHECK(executed.load(std::memory_order_acquire));
}

// -----------------------------------------------------------------------------
// UT-2: Worker-local FrameArena isolation
// -----------------------------------------------------------------------------

void TestWorkerFrameArenaIsolation(Engine::ThreadPool& pool, std::size_t worker_count) {
    Section("UT-2.1: worker frame arenas grow independently");

    std::vector<std::atomic<std::size_t>> peak_used(worker_count);
    for (auto& peak : peak_used) {
        peak.store(0, std::memory_order_relaxed);
    }

    std::atomic<int> tasks_done{0};
    constexpr int kAllocsPerTask = 64;
    constexpr std::size_t kAllocBytes = 4096;

    for (std::size_t target = 0; target < worker_count; ++target) {
        pool.Submit(
            [ &pool, &peak_used, &tasks_done, target ]() {
                for (int i = 0; i < kAllocsPerTask; ++i) {
                    void* p = EngineAllocator::AllocFrame(kAllocBytes, 16);
                    if (p != nullptr) {
                        std::memset(p, 0x11, kAllocBytes);
                    }
                }
                const std::size_t used = pool.WorkerFrameUsedBytes(target);
                std::size_t prev = peak_used[target].load(std::memory_order_relaxed);
                while (used > prev
                       && !peak_used[target].compare_exchange_weak(
                           prev, used, std::memory_order_relaxed)) {
                }
                tasks_done.fetch_add(1, std::memory_order_relaxed);
            },
            Engine::TaskOptions{.target_worker = target, .pinned = true});
    }
    pool.WaitIdle();
    CHECK(tasks_done.load() == static_cast<int>(worker_count));

    for (std::size_t target = 0; target < worker_count; ++target) {
        CHECK(peak_used[target].load() >= kAllocsPerTask * kAllocBytes / 2);
    }
}

void TestResetAllWorkerFrameArenas(Engine::ThreadPool& pool, std::size_t worker_count) {
    Section("UT-2.2: ResetFrameArenas clears all worker arenas");

    pool.Submit(
        []() {
            (void)EngineAllocator::AllocFrame(8192, 64);
        },
        Engine::TaskOptions{.target_worker = 0, .pinned = true});
    pool.WaitIdle();

    CHECK(pool.WorkerFrameUsedBytes(0) > 0);

    EngineAllocator::ResetFrameArenas();

    for (std::size_t w = 0; w < worker_count; ++w) {
        CHECK(pool.WorkerFrameUsedBytes(w) == 0);
        CHECK(pool.WorkerGpuUsedBytes(w) == 0);
    }
    CHECK(EngineAllocator::GetFrameArena().GetUsedBytes() == 0);
}

// -----------------------------------------------------------------------------
// IT-1: Coroutine spanning frames while arenas reset
// -----------------------------------------------------------------------------

struct PoolYieldAwaiter {
    Engine::ThreadPool* pool = nullptr;

    [[nodiscard]] bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) const {
        pool->ResumeCoroutine(handle);
    }

    void await_resume() const noexcept {}
};

Engine::Task<int> MakeSpanningLoadTask(Engine::ThreadPoolScheduler scheduler) {
    std::array<std::uint8_t, 64 * 1024> payload{};
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::uint8_t>((i * 131) & 0xFF);
    }

    co_await Engine::switch_to(scheduler);
    CHECK(payload[12345] == static_cast<std::uint8_t>((12345 * 131) & 0xFF));

    co_await PoolYieldAwaiter{scheduler.GetPool()};
    CHECK(payload[40000] == static_cast<std::uint8_t>((40000 * 131) & 0xFF));

    co_await PoolYieldAwaiter{scheduler.GetPool()};
    CHECK(payload.back() == static_cast<std::uint8_t>(((payload.size() - 1) * 131) & 0xFF));

    co_return static_cast<int>(payload[7777]);
}

void TestCoroutineSurvivesFrameResets(
    Engine::ThreadPool& pool,
    Engine::ThreadPoolScheduler scheduler) {
    Section("IT-1: suspended coroutine survives ResetFrameArenas across frames");

    auto task = MakeSpanningLoadTask(scheduler);
    task.Start();

    for (int frame = 0; frame < 4; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        EngineAllocator::ResetFrameArenas();

        pool.Submit([]() { (void)EngineAllocator::AllocFrame(256, 16); });
    }

    pool.WaitIdle();
    CHECK(task.Result() == static_cast<int>((7777 * 131) & 0xFF));
}

// -----------------------------------------------------------------------------
// IT-2: Jitter idle flush suppression
// -----------------------------------------------------------------------------

void TestJitterFlushSuppression(Engine::ThreadPool& pool, std::size_t worker_count) {
    Section("IT-2: frame-sync flush is bounded during jitter idle");

    Engine::TestDiagnostics::ResetWorkerFlushCounts();

    constexpr int kFrames = 60;
    for (int frame = 0; frame < kFrames; ++frame) {
        pool.Submit([]() {});
        Engine::ThreadPool::IncrementFlushGeneration();
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    pool.WaitIdle();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    for (std::size_t w = 0; w < worker_count; ++w) {
        const std::uint64_t flushes = Engine::TestDiagnostics::GetWorkerFlushCount(w);
        std::printf("  worker %zu flushes: %llu\n", w, static_cast<unsigned long long>(flushes));
        CHECK(flushes <= static_cast<std::uint64_t>(kFrames));
    }
}

// -----------------------------------------------------------------------------
// PT-1: Thread cache reclamation after load spike
// -----------------------------------------------------------------------------

void TestThreadCacheReclamation(Engine::ThreadPool& pool) {
    Section("PT-1: worker thread caches drain after load spike + generation bump");

    const auto stats_before = Engine::QueryMemoryStats();

    std::atomic<int> completed{0};
    constexpr int kWorkers = 8;
    constexpr std::size_t kChunkBytes = 8 * 1024 * 1024;
    for (int i = 0; i < kWorkers; ++i) {
        pool.Submit([ &completed, kChunkBytes ]() {
            void* block = Engine::Allocate(kChunkBytes, 64);
            if (block != nullptr) {
                std::memset(block, 0x42, kChunkBytes);
                Engine::Free(block);
            }
            completed.fetch_add(1, std::memory_order_relaxed);
        });
    }
    pool.WaitIdle();
    CHECK(completed.load() == kWorkers);

    Engine::ThreadPool::IncrementFlushGeneration();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const auto stats_after = Engine::QueryMemoryStats();
    std::printf("  mapped before=%zu after=%zu cached before=%zu after=%zu\n",
                stats_before.bytesMapped,
                stats_after.bytesMapped,
                stats_before.bytesCached,
                stats_after.bytesCached);

    CHECK(stats_after.bytesMapped <= stats_before.bytesMapped + kChunkBytes);
}

// -----------------------------------------------------------------------------
// IT-3: Async asset load with heap lifetime across frame arena resets
// -----------------------------------------------------------------------------

Engine::Task<std::vector<std::uint8_t>> AsyncAssetLoadSimulation(Engine::ThreadPoolScheduler scheduler) {
    co_await Engine::switch_to(scheduler);

    auto* long_lived_data = static_cast<std::uint32_t*>(EngineAllocator::AllocHeap(1024, alignof(std::uint32_t)));
    long_lived_data[0] = 0xDEADBEEFU;

    co_await PoolYieldAwaiter{scheduler.GetPool()};

    CHECK(long_lived_data[0] == 0xDEADBEEFU);

    auto* scratch = static_cast<char*>(EngineAllocator::AllocFrame(256, 16));
    CHECK(scratch != nullptr);
    if (scratch != nullptr) {
        std::strcpy(scratch, "Done");
        CHECK(scratch[0] == 'D');
    }

    std::vector<std::uint8_t> result(4, 0xFF);
    EngineAllocator::FreeHeap(long_lived_data);
    co_return result;
}

void TestAsyncAssetLoadIntegration(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("IT-3: heap load data survives ResetFrameArenas across frames");

    auto task = AsyncAssetLoadSimulation(scheduler);
    task.Start();

    for (int frame = 0; frame < 5; ++frame) {
        for (int t = 0; t < 100; ++t) {
            pool.Submit([]() { (void)EngineAllocator::AllocFrame(64, 16); });
        }
        pool.WaitIdle();
        EngineAllocator::ResetFrameArenas();
    }

    pool.WaitIdle();
    const auto& result = task.Result();
    CHECK(result.size() == 4);
    CHECK(result[0] == 0xFF);
}

// -----------------------------------------------------------------------------
// PT-2: Ten thousand submits should not grow global heap stats
// -----------------------------------------------------------------------------

void TestSubmitDoesNotGrowHeapStats(Engine::ThreadPool& pool) {
    Section("PT-2: 10k Submit calls reuse task pools without heap growth");

    const auto stats_before = Engine::QueryMemoryStats();

    std::atomic<int> counter{0};
    for (int i = 0; i < 10000; ++i) {
        pool.Submit([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    pool.WaitIdle();
    CHECK(counter.load() == 10000);

    const auto stats_after = Engine::QueryMemoryStats();
    std::printf("  mapped before=%zu after=%zu huge before=%zu after=%zu\n",
                stats_before.bytesMapped,
                stats_after.bytesMapped,
                stats_before.bytesHugeAlloc,
                stats_after.bytesHugeAlloc);

    CHECK(stats_after.bytesMapped <= stats_before.bytesMapped + 64 * 1024);
    CHECK(stats_after.bytesHugeAlloc <= stats_before.bytesHugeAlloc + 64 * 1024);
    CHECK(Engine::detail::GetTaskPool32FreeCount() == kTaskPool32Capacity);
}

} // namespace

int main() {
    std::printf("Task / memory integration test suite\n");
    std::fflush(stdout);

    EngineAllocator::Initialize();

    Engine::ThreadPool pool(4);
    const Engine::ThreadPoolScheduler scheduler = pool.GetScheduler();
    const std::size_t worker_count = pool.WorkerCount();

    TestSmallTaskPoolRoundTrip(pool);
    TestLargeTaskPoolRoundTrip(pool);
    TestPoolExhaustionFallback(pool);
    TestFallbackForOversizedCapture(pool);

    TestWorkerFrameArenaIsolation(pool, worker_count);
    TestResetAllWorkerFrameArenas(pool, worker_count);

    TestCoroutineSurvivesFrameResets(pool, scheduler);
    TestAsyncAssetLoadIntegration(pool, scheduler);
    TestJitterFlushSuppression(pool, worker_count);

    TestThreadCacheReclamation(pool);
    TestSubmitDoesNotGrowHeapStats(pool);

    pool.Shutdown();
    EngineAllocator::Shutdown();

    const int checks = g_checks.load();
    const int failures = g_failures.load();
    std::printf("\n----------------------------------------\n");
    std::printf("Checks run : %d\n", checks);
    std::printf("Failures   : %d\n", failures);
    std::printf("Result     : %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
