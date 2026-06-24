#pragma once

// =============================================================================
// ThreadLocalAllocator.hxx
//
// Phase 3: Thread-Local Storage (TLS) cache.
//
//   * Each thread owns a ThreadCache (a `thread_local` instance) holding one
//     intrusive free list per size class. The hot allocate/free path touches
//     only this thread-local state, so it is lock-free.
//   * When a free list runs dry it refills in bulk from the Phase 2
//     CentralMemoryManager (one spin-lock acquisition per batch). When a list
//     grows past a cap it spills a batch back to the central manager so a
//     producer/consumer thread pair cannot leak memory into one cache forever.
//   * On thread exit the `thread_local` destructor flushes every cached block
//     back to the central manager, returning the memory for reuse.
//
// Cross-thread / cross-module frees are correct because the freeing thread
// recovers the size class straight from the chunk header (O(1) pointer mask),
// never from per-thread bookkeeping.
// =============================================================================

#include "CentralMemoryManager.hxx"

#include <cstddef>
#include <cstdint>

namespace Engine::Memory {

// The single process-wide central manager backing every thread cache.
// Intentionally leaked (never destroyed) so a thread_local ThreadCache running
// its destructor at program/thread exit always sees a live manager, sidestepping
// static-destruction-order hazards.
[[nodiscard]] CentralMemoryManager& GlobalCentralManager() noexcept;

class ThreadCache {
public:
    // Accessor for the calling thread's cache.
    [[nodiscard]] static ThreadCache& Get() noexcept;

    [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment);
    void Free(void* ptr);

    // Return every cached block to the central manager (called on thread exit).
    void Flush();

    struct Stats {
        std::size_t cachedBlocks = 0;
        std::size_t cachedBytes  = 0;
    };
    [[nodiscard]] Stats GetStats() const noexcept;

    ThreadCache() = default;
    ~ThreadCache();

    ThreadCache(const ThreadCache&) = delete;
    ThreadCache& operator=(const ThreadCache&) = delete;

private:
    // How many blocks to pull on a refill and the per-class cache ceiling.
    static constexpr std::uint32_t kRefillBatch       = 32;
    static constexpr std::uint32_t kMaxCachedPerClass = 256;

    struct FreeList {
        void*         head  = nullptr; // intrusive: first word of each block
        std::uint32_t count = 0;
    };

    void SpillClass(unsigned sizeClass);

    FreeList m_lists[kNumSizeClasses]{};
};

} // namespace Engine::Memory
