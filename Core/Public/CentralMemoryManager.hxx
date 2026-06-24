#pragma once

// =============================================================================
// CentralMemoryManager.hxx
//
// Phase 2 (+ foundation for Phase 3/4): Central chunk manager.
//
//   * Wraps the Phase 1 PlatformVirtualAllocator and serves memory from
//     2 MiB, 2 MiB-aligned "chunks" reserved+committed straight from the OS.
//   * Every chunk begins with a ChunkHeader followed by an inline allocation
//     bitmap. Because chunks are 2 MiB-aligned, ANY block pointer can be mapped
//     back to its owning header in O(1) with a single mask:
//
//         header = ptr & ~(kChunkSize - 1)
//
//     This is what makes lock-free thread caches (Phase 3) and cross-thread /
//     cross-DLL frees (Phase 4) correct: whoever frees a pointer can always
//     discover its size class without touching shared state.
//   * Size-classed bitmap allocator with a batch API (AcquireBlocks /
//     ReleaseBlocks) so the thread caches can refill in bulk under one lock.
//   * Oversized requests bypass the chunking and get a dedicated, header-tagged
//     reservation (AllocateLarge / FreeLarge).
//   * All shared state is guarded by an std::atomic_flag spin lock.
//
// No virtual dispatch: the OS allocator is selected at compile time and stored
// by value, so the whole manager is monomorphic and fully inlinable.
// =============================================================================

#include "CoreExport.hxx"
#include "EngineVirtualMemory.hxx"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Engine::Memory {

// -----------------------------------------------------------------------------
// SpinLock: ultra-light mutual exclusion built on std::atomic_flag.
// Satisfies the BasicLockable contract so std::lock_guard works with it.
// -----------------------------------------------------------------------------
class SpinLock {
public:
    SpinLock() noexcept = default;
    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    void lock() noexcept {
        while (m_flag.test_and_set(std::memory_order_acquire)) {
            while (m_flag.test(std::memory_order_relaxed)) {
                CpuRelax();
            }
        }
    }

    [[nodiscard]] bool try_lock() noexcept {
        return !m_flag.test_and_set(std::memory_order_acquire);
    }

    void unlock() noexcept {
        m_flag.clear(std::memory_order_release);
    }

private:
    static void CpuRelax() noexcept {
#if defined(_MSC_VER)
        _mm_pause();
#elif defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
        __asm__ __volatile__("yield");
#endif
    }

    std::atomic_flag m_flag = ATOMIC_FLAG_INIT;
};

// -----------------------------------------------------------------------------
// A raw 2 MiB region handed to callers that want to manage their own memory
// (kept distinct from the header-tagged block/large allocations).
// -----------------------------------------------------------------------------
struct RawChunk {
    void*       base = nullptr;
    std::size_t size = 0;

    [[nodiscard]] bool Valid() const noexcept { return base != nullptr; }
};

// -----------------------------------------------------------------------------
// Size-class model.
//
// Block sizes are powers of two from kMinBlockSize (16 B) up to kMaxBlockSize
// (256 KiB). The class index is just the base-2 log minus the minimum shift,
// which keeps the size <-> class mapping branch-free.
// -----------------------------------------------------------------------------
inline constexpr std::size_t kMinBlockSize   = 16;
inline constexpr std::size_t kMaxBlockSize   = std::size_t{256} * 1024; // 256 KiB
inline constexpr unsigned    kMinClassShift  = 4;  // 2^4  == 16
inline constexpr unsigned    kMaxClassShift  = 18; // 2^18 == 256 KiB
inline constexpr unsigned    kNumSizeClasses = kMaxClassShift - kMinClassShift + 1; // 15

// Power-of-two block size that satisfies both size and alignment.
[[nodiscard]] constexpr std::size_t NormalizedBlockSize(std::size_t size,
                                                        std::size_t alignment) noexcept {
    std::size_t required = size > alignment ? size : alignment;
    if (required < kMinBlockSize) required = kMinBlockSize;
    return CeilToPowerOfTwo(required);
}

// blockSize must be a power of two in [kMinBlockSize, kMaxBlockSize].
[[nodiscard]] constexpr unsigned SizeClassIndex(std::size_t blockSize) noexcept {
    return static_cast<unsigned>(std::countr_zero(blockSize)) - kMinClassShift;
}

[[nodiscard]] constexpr std::size_t BlockSizeForClass(unsigned index) noexcept {
    return std::size_t{1} << (index + kMinClassShift);
}

// -----------------------------------------------------------------------------
// Chunk header. Lives at the 2 MiB-aligned base of every managed region so it
// is reachable from any contained pointer by masking.
// -----------------------------------------------------------------------------
inline constexpr std::uint32_t kChunkMagic = 0x474D454Du; // "GMEM"

enum class ChunkKind : std::uint16_t {
    Block = 1, // subdivided into fixed-size blocks tracked by the bitmap
    Large = 2, // a single oversized allocation
};

struct ChunkHeader {
    std::uint32_t  magic      = 0;
    ChunkKind      kind       = ChunkKind::Block;
    std::uint16_t  sizeClass  = 0;
    std::uint32_t  blockSize  = 0;
    std::uint32_t  blockCount = 0;
    std::uint32_t  freeBlocks = 0;
    void*          osBase     = nullptr; // original reservation base (for Release)
    std::size_t    osSize     = 0;       // original reservation size
    std::byte*     blocks     = nullptr; // first block / large payload
    std::uint64_t* bitmap     = nullptr; // inline bitmap (Block chunks only)
};

// Map any pointer handed out by the manager back to its owning header.
[[nodiscard]] inline ChunkHeader* HeaderFromPointer(const void* ptr,
                                                    std::size_t chunkSize) noexcept {
    const auto base = reinterpret_cast<std::uintptr_t>(ptr) & ~(chunkSize - 1);
    return reinterpret_cast<ChunkHeader*>(base);
}

// =============================================================================
// CentralMemoryManager
//
// Deliberately NOT marked GE_API: it stores std::vector / SpinLock members, and
// exporting those across a DLL boundary is precisely the fragility this engine
// is meant to avoid. Phase 4 exposes a flat, ABI-stable entry point
// (Engine::Allocate / Engine::Free) on top of a single shared instance.
// =============================================================================
class CentralMemoryManager {
public:
    static constexpr std::size_t kChunkSize    = std::size_t{2} * 1024 * 1024; // 2 MiB
    static constexpr std::size_t kMaxBlockSize = Engine::Memory::kMaxBlockSize;

    struct Stats {
        std::size_t chunksLive    = 0; // raw chunks currently owned
        std::size_t chunksPooled  = 0; // recycled raw chunks awaiting reuse
        std::size_t blockChunks   = 0; // chunks backing the block allocator
        std::size_t largeBlocks   = 0; // active oversized allocations
        std::size_t bytesReserved = 0; // total address space reserved from OS
    };

    CentralMemoryManager() = default;
    ~CentralMemoryManager();

    CentralMemoryManager(const CentralMemoryManager&) = delete;
    CentralMemoryManager& operator=(const CentralMemoryManager&) = delete;

    void Initialize();
    void Shutdown();

    // ---- Raw chunk API (independent helper, e.g. dedicated sub-arenas) -------
    [[nodiscard]] RawChunk AcquireChunk();
    void ReleaseChunk(const RawChunk& chunk);

    // ---- Size-classed batch API (used by the Phase 3 thread caches) ----------
    // Fills up to `want` block pointers of the given size class; returns the
    // number actually produced (0 on OOM). Blocks come back poisoned-for-free.
    [[nodiscard]] std::uint32_t AcquireBlocks(unsigned sizeClass, void** out, std::uint32_t want);
    // Returns previously acquired blocks (from any chunk / any thread) to free.
    void ReleaseBlocks(void* const* blocks, std::uint32_t count);

    // ---- Oversized allocations ----------------------------------------------
    [[nodiscard]] void* AllocateLarge(std::size_t size, std::size_t alignment);
    void FreeLarge(void* ptr);

    // ---- Convenience single-shot API (direct use / unit tests) ---------------
    [[nodiscard]] void* Allocate(std::size_t size,
                                 std::size_t alignment = alignof(std::max_align_t));
    void Free(void* ptr);

    [[nodiscard]] Stats GetStats() const;

private:
    struct Reservation {
        void*       base    = nullptr; // OS reservation base (to Release)
        void*       aligned = nullptr; // aligned usable base
        std::size_t size    = 0;       // reserved byte count
    };

    [[nodiscard]] Reservation ReserveAligned(std::size_t payloadSize, std::size_t alignment);
    [[nodiscard]] RawChunk ReserveCommitChunk();  // locked callers only
    void ReleaseChunkToOS(const RawChunk& chunk); // locked callers only

    [[nodiscard]] ChunkHeader* CreateBlockChunk(unsigned sizeClass); // locked
    [[nodiscard]] std::uint32_t GrabFromChunk(ChunkHeader* hdr, void** out,
                                              std::uint32_t want);    // locked

    PlatformVirtualAllocator m_os{};
    mutable SpinLock         m_lock{};

    std::vector<RawChunk>     m_pooledChunks; // recycled raw chunks
    std::vector<ChunkHeader*> m_blockChunks;  // all block-allocator chunks
    std::array<std::vector<ChunkHeader*>, kNumSizeClasses> m_chunksByClass{};
    std::vector<ChunkHeader*> m_largeBlocks;  // oversized allocations

    std::size_t m_chunksLive    = 0;
    std::size_t m_bytesReserved = 0;
    bool        m_initialized   = false;
};

static_assert(RawVirtualAllocator<PlatformVirtualAllocator>,
              "CentralMemoryManager requires a conforming OS allocator");

} // namespace Engine::Memory
