#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Engine::TestDiagnostics {

#ifdef ENGINE_TEST_HOOKS
inline std::array<std::atomic<std::uint64_t>, 64>& WorkerFlushCounts() {
    static std::array<std::atomic<std::uint64_t>, 64> counts{};
    return counts;
}

inline void RecordWorkerFlush(std::size_t worker_index) {
    if (worker_index < WorkerFlushCounts().size()) {
        WorkerFlushCounts()[worker_index].fetch_add(1, std::memory_order_relaxed);
    }
}

inline std::uint64_t GetWorkerFlushCount(std::size_t worker_index) {
    if (worker_index >= WorkerFlushCounts().size()) {
        return 0;
    }
    return WorkerFlushCounts()[worker_index].load(std::memory_order_relaxed);
}

inline void ResetWorkerFlushCounts() {
    for (auto& counter : WorkerFlushCounts()) {
        counter.store(0, std::memory_order_relaxed);
    }
}
#else
inline void RecordWorkerFlush(std::size_t) {}
inline std::uint64_t GetWorkerFlushCount(std::size_t) { return 0; }
inline void ResetWorkerFlushCounts() {}
#endif

} // namespace Engine::TestDiagnostics
