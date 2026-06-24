// =============================================================================
// EngineApiTest.cxx
//
// Exercises the Phase 3 (TLS cache) + Phase 4 (module-boundary API) surface
// through the real Core.dll. Linked against Core.lib (so Engine::Allocate/Free
// are imported from Core.dll) and CrossModulePlugin.lib (a second module).
//
// Covered:
//   Test 1   : alignment & boundary through Engine::Allocate
//   Test 2.1 : Core.dll allocates -> EXE frees (and EXE allocates -> plugin frees)
//   Test 2.2 : plugin (module A) allocates -> EXE (module B) frees, both ways
//   Test 3.1 : 32-thread random alloc/free stress through the TLS path
//   Test 3.2 : cross-thread free (allocate on thread A, free on thread B)
// =============================================================================

#include "MemoryAPI.hxx"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

extern "C" {
__declspec(dllimport) void* PluginAllocate(std::size_t size, std::size_t alignment);
__declspec(dllimport) void  PluginFree(void* ptr);
}

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

void Touch(void* p, std::size_t n) { std::memset(p, 0xCD, n); }

// -----------------------------------------------------------------------------
void TestAlignmentAndBoundary() {
    Section("Test 1: alignment & boundary (Engine API)");
    const std::size_t aligns[] = { 1, 4, 16, 64, 16 * 1024 };
    for (std::size_t a : aligns) {
        void* p = Engine::Allocate(a == 1 ? 1 : a, a);
        CHECK(p != nullptr);
        CHECK(IsAligned(p, a));
        Touch(p, a == 1 ? 1 : a);
        Engine::Free(p);
    }
    const std::size_t sizes[] = { 1, 16, 4095, 4096, 4097, 64 * 1024, 2 * 1024 * 1024 };
    for (std::size_t s : sizes) {
        void* p = Engine::Allocate(s, 16);
        CHECK(p != nullptr);
        CHECK(IsAligned(p, 16));
        Touch(p, s);
        Engine::Free(p);
    }
}

// -----------------------------------------------------------------------------
void TestCrossModule() {
    Section("Test 2: cross-module allocate/free (EXE <-> plugin DLL)");

    // EXE allocates (Core.dll) -> plugin DLL frees.
    for (int i = 0; i < 1000; ++i) {
        void* p = Engine::Allocate(96 + (i % 7) * 32, 16);
        CHECK(p != nullptr);
        Touch(p, 96);
        PluginFree(p);
    }

    // Plugin DLL allocates -> EXE frees.
    for (int i = 0; i < 1000; ++i) {
        void* p = PluginAllocate(128 + (i % 5) * 64, 32);
        CHECK(p != nullptr);
        CHECK(IsAligned(p, 32));
        Touch(p, 128);
        Engine::Free(p);
    }

    // Oversized blocks across the boundary too.
    void* big = PluginAllocate(1024 * 1024, 4096);
    CHECK(big != nullptr);
    CHECK(IsAligned(big, 4096));
    Touch(big, 1024 * 1024);
    Engine::Free(big);
}

// -----------------------------------------------------------------------------
void TestConcurrencyStress() {
    Section("Test 3.1: 32-thread stress (TLS path)");
    constexpr int kThreads = 32;
    constexpr int kIterations = 10000;
    std::atomic<int> bad{0};

    auto worker = [&bad](unsigned seed) {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<std::size_t> sizeDist(1, 8192);
        std::uniform_int_distribution<int> alignPow(0, 6);

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
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) threads.emplace_back(worker, 0x2000u + t);
    for (auto& th : threads) th.join();

    CHECK(bad.load() == 0);
    const auto stats = Engine::QueryMemoryStats();
    std::printf("  stats: blockChunks=%zu largeBlocks=%zu reserved=%zu bytes\n",
                stats.blockChunks, stats.largeBlocks, stats.bytesReserved);
}

// -----------------------------------------------------------------------------
void TestCrossThreadFree() {
    Section("Test 3.2: cross-thread free (Engine API)");
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

} // namespace

int main() {
    std::printf("Engine memory API (TLS + module boundary) test suite\n");

    TestAlignmentAndBoundary();
    TestCrossModule();
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
