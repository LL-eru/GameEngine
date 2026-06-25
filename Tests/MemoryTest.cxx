// =============================================================================
// MemoryTest.cxx
//
// Unit/integration tests for the post-rpmalloc allocator stack:
//   1. FrameArena  : OS-direct bump, absolute-address alignment, exhaustion,
//                    reset reuse.
//   2. GPUArena    : 256 B default alignment bump.
//   3. ObjectPool  : fixed-size free list, Contains/boundary, reuse.
//   4. Engine::Allocate (rpmalloc) : alignment & boundary, multi-thread stress,
//                    cross-thread free.
//   5. EngineAllocator pool manager : CreatePool / AllocPool / FreePool handles.
//   6. HostServices bindings        : CoreAllocHeap / CoreAllocFrame / Åc
//
// A tiny CHECK harness reports pass/fail; the process exit code reflects it.
// =============================================================================

#include "EngineAllocator.hxx"
#include "CoreInit.hxx"
#include "MemoryAPI.hxx"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
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

void Section(const char* name) { std::printf("\n== %s ==\n", name); }

bool IsAligned(const void* p, std::size_t a) {
    return (reinterpret_cast<std::uintptr_t>(p) & (a - 1)) == 0;
}

void Touch(void* p, std::size_t n) { std::memset(p, 0xAB, n); }

// -----------------------------------------------------------------------------
void TestFrameArena() {
    Section("Test 1: FrameArena (OS-direct bump)");
    FrameArena arena;
    arena.Initialize(1 * 1024 * 1024);

    // Various alignments must be honoured against the absolute address.
    const std::size_t aligns[] = { 1, 4, 16, 64, 256, 4096 };
    for (std::size_t a : aligns) {
        void* p = arena.Allocate(a, a);
        CHECK(p != nullptr);
        CHECK(IsAligned(p, a));
        if (p) Touch(p, a);
    }

    // Monotonic growth: two allocations never overlap.
    void* a1 = arena.Allocate(1000, 16);
    void* a2 = arena.Allocate(1000, 16);
    CHECK(a1 != nullptr && a2 != nullptr);
    CHECK(a1 != a2);
    CHECK(static_cast<unsigned char*>(a2) >= static_cast<unsigned char*>(a1) + 1000);

    // Exhaustion returns nullptr rather than corrupting.
    void* huge = arena.Allocate(4 * 1024 * 1024, 16);
    CHECK(huge == nullptr);

    // Reset rewinds the cursor; the next allocation reuses the base.
    const std::size_t usedBefore = arena.GetUsedBytes();
    CHECK(usedBefore > 0);
    arena.Reset();
    CHECK(arena.GetUsedBytes() == 0);
    void* afterReset = arena.Allocate(64, 64);
    CHECK(afterReset != nullptr);
    CHECK(IsAligned(afterReset, 64));

    arena.Shutdown();
    CHECK(arena.GetCapacityBytes() == 0);
}

// -----------------------------------------------------------------------------
void TestGpuArena() {
    Section("Test 2: GPUArena (256 B default alignment)");
    GPUArena arena;
    arena.Initialize(2 * 1024 * 1024);

    for (int i = 0; i < 16; ++i) {
        void* p = arena.Allocate(500 + i * 37);
        CHECK(p != nullptr);
        CHECK(IsAligned(p, 256));
        if (p) Touch(p, 500);
    }
    arena.Reset();
    CHECK(arena.GetUsedBytes() == 0);
    arena.Shutdown();
}

// -----------------------------------------------------------------------------
void TestObjectPool() {
    Section("Test 3: ObjectPool (fixed-size free list)");
    ObjectPool pool;
    pool.Initialize(48, 8); // objectSize rounded up internally if needed

    std::vector<void*> live;
    for (int i = 0; i < 8; ++i) {
        void* p = pool.Allocate();
        CHECK(p != nullptr);
        CHECK(pool.Contains(p));
        live.push_back(p);
    }
    // Capacity reached -> nullptr.
    CHECK(pool.Allocate() == nullptr);

    // Foreign pointer is rejected (not in storage).
    int stackVar = 0;
    CHECK(!pool.Contains(&stackVar));

    // Free everything back and re-acquire the same slots.
    for (void* p : live) pool.Free(p);
    void* reused = pool.Allocate();
    CHECK(reused != nullptr);
    CHECK(pool.Contains(reused));

    pool.Shutdown();
}

// -----------------------------------------------------------------------------
void TestEngineAllocate() {
    Section("Test 4: Engine::Allocate (rpmalloc) alignment & boundary");
    const std::size_t aligns[] = { 1, 4, 16, 64, 256, 4096 };
    for (std::size_t a : aligns) {
        void* p = Engine::Allocate(a == 1 ? 1 : a, a);
        CHECK(p != nullptr);
        CHECK(IsAligned(p, a));
        if (p) Touch(p, a == 1 ? 1 : a);
        Engine::Free(p);
    }
    const std::size_t sizes[] = { 1, 16, 4095, 4096, 4097, 64 * 1024, 2 * 1024 * 1024 };
    for (std::size_t s : sizes) {
        void* p = Engine::Allocate(s, 16);
        CHECK(p != nullptr);
        CHECK(IsAligned(p, 16));
        if (p) Touch(p, s);
        Engine::Free(p);
    }
}

// -----------------------------------------------------------------------------
void TestConcurrencyStress() {
    Section("Test 5: 32-thread random alloc/free stress (rpmalloc)");
    constexpr int kThreads = 32;
    constexpr int kIterations = 10000;
    std::atomic<int> bad{0};

    auto worker = [&bad](unsigned seed) {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<std::size_t> sizeDist(1, 8192);
        std::uniform_int_distribution<int> alignPow(0, 6); // 1..64

        std::vector<std::pair<void*, std::size_t>> live;
        live.reserve(64);
        for (int i = 0; i < kIterations; ++i) {
            if (!live.empty() && (rng() & 1)) {
                auto idx = rng() % live.size();
                Engine::Free(live[idx].first);
                live[idx] = live.back();
                live.pop_back();
            } else {
                const std::size_t size = sizeDist(rng);
                const std::size_t align = std::size_t{1} << alignPow(rng);
                void* p = Engine::Allocate(size, align);
                if (p == nullptr || !IsAligned(p, align)) {
                    bad.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                std::memset(p, static_cast<int>(seed & 0xFF), size);
                live.emplace_back(p, size);
            }
        }
        for (auto& e : live) Engine::Free(e.first);
        Engine::FlushThreadCache();
    };

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) threads.emplace_back(worker, 0x1000u + t);
    for (auto& th : threads) th.join();

    CHECK(bad.load() == 0);
    const auto stats = Engine::QueryMemoryStats();
    std::printf("  stats: mapped=%zu cached=%zu huge=%zu\n",
                stats.bytesMapped, stats.bytesCached, stats.bytesHugeAlloc);
}

// -----------------------------------------------------------------------------
void TestCrossThreadFree() {
    Section("Test 6: cross-thread free (allocate in A, free in B)");
    constexpr int kCount = 8192;
    std::vector<void*> handoff(kCount, nullptr);
    std::atomic<int> bad{0};

    std::thread producer([&] {
        for (int i = 0; i < kCount; ++i) {
            void* p = Engine::Allocate(200, 64);
            if (p == nullptr || !IsAligned(p, 64)) bad.fetch_add(1, std::memory_order_relaxed);
            handoff[i] = p;
        }
    });
    producer.join();

    std::thread consumer([&] {
        for (int i = 0; i < kCount; ++i) {
            if (handoff[i]) Engine::Free(handoff[i]);
        }
    });
    consumer.join();

    CHECK(bad.load() == 0);
}

// -----------------------------------------------------------------------------
void TestPoolManager() {
    Section("Test 7: PoolHandle manager (multiple sizes)");
    CoreInitGame();
    HostServices* hs = CoreGetHostServices();
    CHECK(hs != nullptr);

    PoolHandle particlePool = hs->CreatePool(32, 256);
    PoolHandle transformPool = hs->CreatePool(64, 128);
    PoolHandle statePool = hs->CreatePool(128, 64);
    CHECK(particlePool != nullptr);
    CHECK(transformPool != nullptr);
    CHECK(statePool != nullptr);

    void* p32 = hs->AllocPool(particlePool);
    void* p64 = hs->AllocPool(transformPool);
    void* p128 = hs->AllocPool(statePool);
    CHECK(p32 != nullptr && p64 != nullptr && p128 != nullptr);

    hs->FreePool(particlePool, p32);
    hs->FreePool(transformPool, p64);
    hs->FreePool(statePool, p128);

    hs->DestroyPool(particlePool);
    hs->DestroyPool(transformPool);
    hs->DestroyPool(statePool);
    CHECK(hs->AllocPool(particlePool) == nullptr);

    CoreShutdown();
}

// -----------------------------------------------------------------------------
void TestHostServicesBindings() {
    Section("Test 8: HostServices typed API (heap / frame / gpu / pool)");
    CoreInitGame();
    HostServices* hs = CoreGetHostServices();
    CHECK(hs != nullptr);

    void* heap = hs->AllocHeap(256, 64);
    CHECK(heap != nullptr);
    CHECK(IsAligned(heap, 64));
    hs->FreeHeap(heap);

    void* frame = hs->AllocFrame(128, 16);
    CHECK(frame != nullptr);
    CHECK(IsAligned(frame, 16));
    hs->ResetFrameArenas();
    void* frameAfterReset = hs->AllocFrame(64, 64);
    CHECK(frameAfterReset != nullptr);
    CHECK(IsAligned(frameAfterReset, 64));

    void* gpu = hs->AllocGpu(500, 256);
    CHECK(gpu != nullptr);
    CHECK(IsAligned(gpu, 256));

    PoolHandle pool = hs->CreatePool(48, 4);
    CHECK(pool != nullptr);
    void* slot = hs->AllocPool(pool);
    CHECK(slot != nullptr);
    hs->FreePool(pool, slot);
    hs->DestroyPool(pool);

    CoreShutdown();
}

} // namespace

int main() {
    std::printf("Engine allocator (rpmalloc + arenas + pool) test suite\n");

    TestFrameArena();
    TestGpuArena();
    TestObjectPool();
    TestEngineAllocate();
    TestConcurrencyStress();
    TestCrossThreadFree();
    TestPoolManager();
    TestHostServicesBindings();

    const int checks = g_checks.load();
    const int failures = g_failures.load();
    std::printf("\n----------------------------------------\n");
    std::printf("Checks run : %d\n", checks);
    std::printf("Failures   : %d\n", failures);
    std::printf("Result     : %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
