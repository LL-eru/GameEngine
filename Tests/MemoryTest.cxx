// =============================================================================
// MemoryTest.cxx
//
// Self-contained test driver for Phase 1 (EngineVirtualMemory) and Phase 2
// (CentralMemoryManager). No external test framework is used so the allocator
// core keeps its "standard library only" promise; a tiny CHECK harness reports
// pass/fail and the process exit code reflects the result.
//
// Covered (subset of the project test matrix applicable to Phase 1 & 2):
//   1. Basic alignment & boundary (Unit)
//   2. Multi-thread stress & contention (Concurrency)
//   3. Cross-thread free safety
// DLL-boundary tests belong to Phase 4 and are intentionally out of scope here.
// =============================================================================

#include "CentralMemoryManager.hxx"
#include "EngineVirtualMemory.hxx"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

using namespace Engine::Memory;

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
}

// Touch the whole allocation so we crash here (not later) if commit is broken.
void TouchMemory(void* ptr, std::size_t size) {
    std::memset(ptr, 0xAB, size);
}

// -----------------------------------------------------------------------------
// Phase 1: alignment helpers + OS allocator round-trip
// -----------------------------------------------------------------------------
void TestPhase1() {
    Section("Phase 1: alignment helpers");
    CHECK(IsPowerOfTwo(1));
    CHECK(IsPowerOfTwo(4096));
    CHECK(!IsPowerOfTwo(0));
    CHECK(!IsPowerOfTwo(3));
    CHECK(CeilToPowerOfTwo(0) == 1);
    CHECK(CeilToPowerOfTwo(1) == 1);
    CHECK(CeilToPowerOfTwo(5) == 8);
    CHECK(CeilToPowerOfTwo(4095) == 4096);
    CHECK(CeilToPowerOfTwo(4097) == 8192);
    CHECK(AlignUp(std::size_t{1}, 16) == 16);
    CHECK(AlignUp(std::size_t{16}, 16) == 16);
    CHECK(AlignUp(std::size_t{17}, 16) == 32);

    Section("Phase 1: OS virtual allocator round-trip");
    PlatformVirtualAllocator os;
    const std::size_t pageSize = os.PageSize();
    CHECK(IsPowerOfTwo(pageSize));
    CHECK(pageSize >= 4096);

    const std::size_t reserveSize = pageSize * 16;
    void* region = os.Reserve(reserveSize);
    CHECK(region != nullptr);
    CHECK(IsAligned(region, pageSize));

    CHECK(os.Commit(region, reserveSize));
    TouchMemory(region, reserveSize); // must not fault
    os.Decommit(region, reserveSize);
    os.Release(region, reserveSize);
}

// -----------------------------------------------------------------------------
// Phase 2 / Test 1: basic alignment & boundary
// -----------------------------------------------------------------------------
void TestAlignmentAndBoundary() {
    Section("Test 1: alignment & boundary");
    CentralMemoryManager mgr;
    mgr.Initialize();

    const std::size_t alignments[] = { 1, 4, 16, 64, 16 * 1024 };
    for (std::size_t a : alignments) {
        void* p = mgr.Allocate(a == 1 ? 1 : a, a);
        CHECK(p != nullptr);
        CHECK(IsAligned(p, a));
        TouchMemory(p, a == 1 ? 1 : a);
        mgr.Free(p);
    }

    // Page-boundary-crossing sizes.
    const std::size_t sizes[] = { 1, 4, 16, 64, 4095, 4096, 4097, 64 * 1024 };
    for (std::size_t s : sizes) {
        void* p = mgr.Allocate(s, 16);
        CHECK(p != nullptr);
        CHECK(IsAligned(p, 16));
        TouchMemory(p, s); // exercises every committed byte across page borders
        mgr.Free(p);
    }

    // Large path (exceeds kMaxBlockSize -> dedicated OS reservation).
    void* big = mgr.Allocate(2 * 1024 * 1024, 4096);
    CHECK(big != nullptr);
    CHECK(IsAligned(big, 4096));
    TouchMemory(big, 2 * 1024 * 1024);
    mgr.Free(big);

    mgr.Shutdown();
}

// -----------------------------------------------------------------------------
// Phase 2: raw chunk acquire/release + recycling
// -----------------------------------------------------------------------------
void TestRawChunks() {
    Section("Phase 2: raw 2 MiB chunk pool");
    CentralMemoryManager mgr;
    mgr.Initialize();

    RawChunk a = mgr.AcquireChunk();
    CHECK(a.Valid());
    CHECK(a.size == CentralMemoryManager::kChunkSize);
    TouchMemory(a.base, a.size);

    RawChunk b = mgr.AcquireChunk();
    CHECK(b.Valid());
    CHECK(a.base != b.base);

    mgr.ReleaseChunk(a);
    // Releasing pools the chunk; the next acquire should hand it straight back.
    RawChunk c = mgr.AcquireChunk();
    CHECK(c.base == a.base);

    mgr.ReleaseChunk(b);
    mgr.ReleaseChunk(c);
    mgr.Shutdown();
}

// -----------------------------------------------------------------------------
// Test 3 / case 1: 32-thread random alloc/free stress
// -----------------------------------------------------------------------------
void TestConcurrencyStress() {
    Section("Test 3.1: 32-thread random alloc/free stress");
    CentralMemoryManager mgr;
    mgr.Initialize();

    constexpr int kThreads = 32;
    constexpr int kIterations = 10000;
    std::atomic<int> badPointers{0};

    auto worker = [&mgr, &badPointers](unsigned seed) {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<std::size_t> sizeDist(1, 8192);
        std::uniform_int_distribution<int> alignPow(0, 6); // 1..64

        std::vector<std::pair<void*, std::size_t>> live;
        live.reserve(64);

        for (int i = 0; i < kIterations; ++i) {
            const bool doFree = !live.empty() && (rng() & 1);
            if (doFree) {
                auto idx = rng() % live.size();
                mgr.Free(live[idx].first);
                live[idx] = live.back();
                live.pop_back();
            } else {
                const std::size_t size = sizeDist(rng);
                const std::size_t align = std::size_t{1} << alignPow(rng);
                void* p = mgr.Allocate(size, align);
                if (p == nullptr || !IsAligned(p, align)) {
                    badPointers.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                // Write a thread-unique pattern to detect overlap/corruption.
                std::memset(p, static_cast<int>(seed & 0xFF), size);
                live.emplace_back(p, size);
            }
        }
        for (auto& entry : live) mgr.Free(entry.first);
    };

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back(worker, static_cast<unsigned>(0x1000 + t));
    }
    for (auto& th : threads) th.join();

    CHECK(badPointers.load() == 0);

    const auto stats = mgr.GetStats();
    std::printf("  stats: blockChunks=%zu largeBlocks=%zu reserved=%zu bytes\n",
                stats.blockChunks, stats.largeBlocks, stats.bytesReserved);

    mgr.Shutdown();
}

// -----------------------------------------------------------------------------
// Test 3 / case 2: cross-thread free (allocate in A, free in B)
// -----------------------------------------------------------------------------
void TestCrossThreadFree() {
    Section("Test 3.2: cross-thread free");
    CentralMemoryManager mgr;
    mgr.Initialize();

    constexpr int kCount = 4096;
    std::vector<void*> handoff(kCount, nullptr);
    std::atomic<int> mismatches{0};

    std::thread producer([&] {
        for (int i = 0; i < kCount; ++i) {
            void* p = mgr.Allocate(128, 64);
            if (p == nullptr || !IsAligned(p, 64)) {
                mismatches.fetch_add(1, std::memory_order_relaxed);
            }
            handoff[i] = p;
        }
    });
    producer.join(); // establish a clean happens-before for the handoff buffer

    std::thread consumer([&] {
        for (int i = 0; i < kCount; ++i) {
            if (handoff[i]) mgr.Free(handoff[i]);
        }
    });
    consumer.join();

    CHECK(mismatches.load() == 0);
    mgr.Shutdown();
}

} // namespace

int main() {
    std::printf("EngineVirtualMemory / CentralMemoryManager test suite\n");

    TestPhase1();
    TestAlignmentAndBoundary();
    TestRawChunks();
    TestConcurrencyStress();
    TestCrossThreadFree();

    const int checks = g_checks.load();
    const int failures = g_failures.load();
    std::printf("\n----------------------------------------\n");
    std::printf("Checks run : %d\n", checks);
    std::printf("Failures   : %d\n", failures);
    std::printf("Result     : %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
