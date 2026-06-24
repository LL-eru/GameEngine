#include "Public/CentralMemoryManager.hxx"

#include <algorithm>
#include <cstring>
#include <mutex> // std::lock_guard

namespace Engine::Memory {

namespace {
constexpr std::size_t BitmapBytesFor(std::size_t maxBlocks) noexcept {
    return ((maxBlocks + 63) / 64) * sizeof(std::uint64_t);
}

// First sizeof(void*) bytes of a cached/free block stay accessible so the
// thread cache can thread its intrusive free list through them; the remainder
// is what we poison for ASan.
constexpr std::size_t kLinkBytes = sizeof(void*);

inline void PoisonFreeBlock(void* block, std::size_t blockSize) noexcept {
    if (blockSize > kLinkBytes) {
        ENGINE_ASAN_POISON(static_cast<std::byte*>(block) + kLinkBytes,
                           blockSize - kLinkBytes);
    }
}
} // namespace

CentralMemoryManager::~CentralMemoryManager() {
    Shutdown();
}

void CentralMemoryManager::Initialize() {
    std::lock_guard<SpinLock> guard(m_lock);
    m_initialized = true;
}

void CentralMemoryManager::Shutdown() {
    std::lock_guard<SpinLock> guard(m_lock);

    for (ChunkHeader* hdr : m_blockChunks) {
        if (hdr && hdr->osBase) {
            m_os.Release(hdr->osBase, hdr->osSize);
        }
    }
    m_blockChunks.clear();
    for (auto& list : m_chunksByClass) list.clear();

    for (ChunkHeader* hdr : m_largeBlocks) {
        if (hdr && hdr->osBase) {
            m_os.Release(hdr->osBase, hdr->osSize);
        }
    }
    m_largeBlocks.clear();

    for (const RawChunk& chunk : m_pooledChunks) {
        ReleaseChunkToOS(chunk);
    }
    m_pooledChunks.clear();

    m_chunksLive = 0;
    m_bytesReserved = 0;
    m_initialized = false;
}

// -----------------------------------------------------------------------------
// OS reservation helpers
// -----------------------------------------------------------------------------
CentralMemoryManager::Reservation
CentralMemoryManager::ReserveAligned(std::size_t payloadSize, std::size_t alignment) {
    Reservation r{};
    // Over-reserve by `alignment` so we can always shift the base up to an
    // `alignment` boundary and still fit `payloadSize`.
    const std::size_t reserveSize = AlignUp(payloadSize + alignment, m_os.PageSize());
    void* base = m_os.Reserve(reserveSize);
    if (base == nullptr) return r;
    r.base = base;
    r.aligned = AlignUp(base, alignment);
    r.size = reserveSize;
    return r;
}

RawChunk CentralMemoryManager::ReserveCommitChunk() {
    RawChunk chunk{};
    void* base = m_os.Reserve(kChunkSize);
    if (base == nullptr) {
        ENGINE_VM_VERIFY(false, "OS failed to reserve a 2 MiB chunk");
        return chunk;
    }
    if (!m_os.Commit(base, kChunkSize)) {
        ENGINE_VM_VERIFY(false, "OS failed to commit a 2 MiB chunk");
        m_os.Release(base, kChunkSize);
        return chunk;
    }
    chunk.base = base;
    chunk.size = kChunkSize;
    m_chunksLive += 1;
    m_bytesReserved += kChunkSize;
    return chunk;
}

void CentralMemoryManager::ReleaseChunkToOS(const RawChunk& chunk) {
    if (!chunk.Valid()) return;
    m_os.Release(chunk.base, chunk.size);
    if (m_chunksLive > 0) m_chunksLive -= 1;
    if (m_bytesReserved >= chunk.size) m_bytesReserved -= chunk.size;
}

RawChunk CentralMemoryManager::AcquireChunk() {
    std::lock_guard<SpinLock> guard(m_lock);
    if (!m_pooledChunks.empty()) {
        RawChunk chunk = m_pooledChunks.back();
        m_pooledChunks.pop_back();
        return chunk;
    }
    return ReserveCommitChunk();
}

void CentralMemoryManager::ReleaseChunk(const RawChunk& chunk) {
    if (!chunk.Valid()) return;
    std::lock_guard<SpinLock> guard(m_lock);
    m_pooledChunks.push_back(chunk);
}

// -----------------------------------------------------------------------------
// Block chunk construction
// -----------------------------------------------------------------------------
ChunkHeader* CentralMemoryManager::CreateBlockChunk(unsigned sizeClass) {
    const std::size_t blockSize = BlockSizeForClass(sizeClass);

    // 2 MiB payload, aligned to 2 MiB so HeaderFromPointer() works.
    Reservation r = ReserveAligned(kChunkSize, kChunkSize);
    if (r.aligned == nullptr) {
        ENGINE_VM_VERIFY(false, "OS failed to reserve an aligned block chunk");
        return nullptr;
    }
    if (!m_os.Commit(r.aligned, kChunkSize)) {
        ENGINE_VM_VERIFY(false, "OS failed to commit a block chunk");
        m_os.Release(r.base, r.size);
        return nullptr;
    }

    auto* chunkBytes = static_cast<std::byte*>(r.aligned);
    auto* hdr = reinterpret_cast<ChunkHeader*>(chunkBytes);

    // Layout: [header][bitmap][padding][blocks...]. Size the bitmap for the
    // worst case (kChunkSize / blockSize) so it always covers the real count.
    const std::size_t maxBlocks   = kChunkSize / blockSize;
    const std::size_t bitmapBytes = BitmapBytesFor(maxBlocks);
    const std::size_t bitmapOffset = AlignUp(sizeof(ChunkHeader), alignof(std::uint64_t));
    const std::size_t blocksOffset = AlignUp(bitmapOffset + bitmapBytes, blockSize);
    const std::uint32_t blockCount =
        static_cast<std::uint32_t>((kChunkSize - blocksOffset) / blockSize);

    ENGINE_VM_VERIFY(blockCount > 0, "chunk too small for the requested block size");

    hdr->magic      = kChunkMagic;
    hdr->kind       = ChunkKind::Block;
    hdr->sizeClass  = static_cast<std::uint16_t>(sizeClass);
    hdr->blockSize  = static_cast<std::uint32_t>(blockSize);
    hdr->blockCount = blockCount;
    hdr->freeBlocks = blockCount;
    hdr->osBase     = r.base;
    hdr->osSize     = r.size;
    hdr->blocks     = chunkBytes + blocksOffset;
    hdr->bitmap     = reinterpret_cast<std::uint64_t*>(chunkBytes + bitmapOffset);

    std::memset(hdr->bitmap, 0, bitmapBytes);

    // Everything starts life as free -> poisoned (link slot stays usable).
    for (std::uint32_t i = 0; i < blockCount; ++i) {
        PoisonFreeBlock(hdr->blocks + static_cast<std::size_t>(i) * blockSize, blockSize);
    }

    m_blockChunks.push_back(hdr);
    m_chunksByClass[sizeClass].push_back(hdr);
    m_chunksLive += 1;
    m_bytesReserved += r.size;
    return hdr;
}

// Grab up to `want` free blocks from a single chunk. Caller holds the lock.
std::uint32_t CentralMemoryManager::GrabFromChunk(ChunkHeader* hdr, void** out,
                                                  std::uint32_t want) {
    std::uint32_t produced = 0;
    const std::size_t blockSize = hdr->blockSize;
    const std::uint32_t words = (hdr->blockCount + 63) / 64;

    for (std::uint32_t w = 0; w < words && produced < want && hdr->freeBlocks > 0; ++w) {
        std::uint64_t bits = hdr->bitmap[w];
        bool stop = false;
        while (produced < want && bits != ~std::uint64_t{0}) {
            const unsigned bit = static_cast<unsigned>(std::countr_one(bits));
            const std::uint32_t index = w * 64 + bit;
            if (index >= hdr->blockCount) { stop = true; break; } // padding bits
            bits |= (std::uint64_t{1} << bit);
            out[produced++] = hdr->blocks + static_cast<std::size_t>(index) * blockSize;
            hdr->freeBlocks -= 1;
        }
        hdr->bitmap[w] = bits;
        if (stop) break;
    }
    return produced;
}

std::uint32_t CentralMemoryManager::AcquireBlocks(unsigned sizeClass, void** out,
                                                  std::uint32_t want) {
    if (want == 0) return 0;
    ENGINE_VM_VERIFY(sizeClass < kNumSizeClasses, "size class out of range");

    std::lock_guard<SpinLock> guard(m_lock);
    auto& list = m_chunksByClass[sizeClass];
    std::uint32_t produced = 0;

    while (produced < want) {
        ChunkHeader* hdr = nullptr;
        for (ChunkHeader* c : list) {
            if (c->freeBlocks > 0) { hdr = c; break; }
        }
        if (hdr == nullptr) {
            hdr = CreateBlockChunk(sizeClass);
            if (hdr == nullptr) break; // OOM
        }
        const std::uint32_t before = produced;
        produced += GrabFromChunk(hdr, out + produced, want - produced);
        if (produced == before) break; // no progress; avoid spinning forever
    }
    return produced;
}

void CentralMemoryManager::ReleaseBlocks(void* const* blocks, std::uint32_t count) {
    if (count == 0) return;
    std::lock_guard<SpinLock> guard(m_lock);

    for (std::uint32_t i = 0; i < count; ++i) {
        void* ptr = blocks[i];
        if (ptr == nullptr) continue;

        ChunkHeader* hdr = HeaderFromPointer(ptr, kChunkSize);
        ENGINE_VM_VERIFY(hdr->magic == kChunkMagic && hdr->kind == ChunkKind::Block,
                         "ReleaseBlocks received a non-block pointer");

        const std::size_t offset =
            static_cast<std::size_t>(static_cast<std::byte*>(ptr) - hdr->blocks);
        ENGINE_VM_VERIFY(offset % hdr->blockSize == 0, "pointer not on a block boundary");

        const std::uint32_t index = static_cast<std::uint32_t>(offset / hdr->blockSize);
        const std::uint32_t word = index / 64;
        const std::uint64_t mask = std::uint64_t{1} << (index % 64);

        ENGINE_VM_VERIFY((hdr->bitmap[word] & mask) != 0, "double free or corruption");
        hdr->bitmap[word] &= ~mask;
        hdr->freeBlocks += 1;
        PoisonFreeBlock(ptr, hdr->blockSize);
    }
}

// -----------------------------------------------------------------------------
// Oversized allocations (one dedicated, header-tagged reservation each)
// -----------------------------------------------------------------------------
void* CentralMemoryManager::AllocateLarge(std::size_t size, std::size_t alignment) {
    if (size == 0) return nullptr;

    std::lock_guard<SpinLock> guard(m_lock);

    const std::size_t pageSize = m_os.PageSize();
    const std::size_t effAlign = (std::max)(alignment, pageSize);

    // Reserve room for the header (rounded up to the alignment so the payload
    // lands on an aligned boundary) plus the payload, all in a 2 MiB-aligned
    // region so the returned pointer still masks back to the header.
    const std::size_t headerReserve = AlignUp(sizeof(ChunkHeader), effAlign);
    const std::size_t payload = headerReserve + size;

    Reservation r = ReserveAligned(payload, kChunkSize);
    if (r.aligned == nullptr) {
        ENGINE_VM_VERIFY(false, "OS failed to reserve a large block");
        return nullptr;
    }
    const std::size_t commitBytes = AlignUp(payload, pageSize);
    if (!m_os.Commit(r.aligned, commitBytes)) {
        ENGINE_VM_VERIFY(false, "OS failed to commit a large block");
        m_os.Release(r.base, r.size);
        return nullptr;
    }

    auto* base = static_cast<std::byte*>(r.aligned);
    auto* hdr = reinterpret_cast<ChunkHeader*>(base);
    void* userPtr = base + headerReserve;

    hdr->magic      = kChunkMagic;
    hdr->kind       = ChunkKind::Large;
    hdr->sizeClass  = 0xFFFF;
    hdr->blockSize  = 0;
    hdr->blockCount = 1;
    hdr->freeBlocks = 0;
    hdr->osBase     = r.base;
    hdr->osSize     = r.size;
    hdr->blocks     = static_cast<std::byte*>(userPtr);
    hdr->bitmap     = nullptr;

    ENGINE_VM_VERIFY(IsAligned(userPtr, alignment), "large block alignment failed");
    ENGINE_VM_VERIFY(HeaderFromPointer(userPtr, kChunkSize) == hdr,
                     "large payload must mask back to its header");

    m_largeBlocks.push_back(hdr);
    m_bytesReserved += r.size;

    ENGINE_ASAN_UNPOISON(userPtr, size);
    return userPtr;
}

void CentralMemoryManager::FreeLarge(void* ptr) {
    if (ptr == nullptr) return;
    std::lock_guard<SpinLock> guard(m_lock);

    ChunkHeader* hdr = HeaderFromPointer(ptr, kChunkSize);
    ENGINE_VM_VERIFY(hdr->magic == kChunkMagic && hdr->kind == ChunkKind::Large,
                     "FreeLarge received a non-large pointer");

    auto it = std::find(m_largeBlocks.begin(), m_largeBlocks.end(), hdr);
    if (it != m_largeBlocks.end()) {
        m_largeBlocks.erase(it);
    }

    void* osBase = hdr->osBase;
    const std::size_t osSize = hdr->osSize;
    if (m_bytesReserved >= osSize) m_bytesReserved -= osSize;
    m_os.Release(osBase, osSize);
}

// -----------------------------------------------------------------------------
// Single-shot convenience API
// -----------------------------------------------------------------------------
void* CentralMemoryManager::Allocate(std::size_t size, std::size_t alignment) {
    if (size == 0) return nullptr;
    ENGINE_VM_VERIFY(IsPowerOfTwo(alignment), "alignment must be a power of two");

    const std::size_t blockSize = NormalizedBlockSize(size, alignment);
    if (blockSize > kMaxBlockSize) {
        return AllocateLarge(size, alignment);
    }

    const unsigned sizeClass = SizeClassIndex(blockSize);
    void* ptr = nullptr;
    if (AcquireBlocks(sizeClass, &ptr, 1) == 0) {
        return nullptr;
    }
    ENGINE_ASAN_UNPOISON(ptr, blockSize);
    ENGINE_VM_VERIFY(IsAligned(ptr, alignment), "block alignment failed");
    return ptr;
}

void CentralMemoryManager::Free(void* ptr) {
    if (ptr == nullptr) return;

    ChunkHeader* hdr = HeaderFromPointer(ptr, kChunkSize);
    ENGINE_VM_VERIFY(hdr->magic == kChunkMagic, "Free of a pointer this manager never produced");

    if (hdr->kind == ChunkKind::Large) {
        FreeLarge(ptr);
        return;
    }
    ReleaseBlocks(&ptr, 1);
}

CentralMemoryManager::Stats CentralMemoryManager::GetStats() const {
    std::lock_guard<SpinLock> guard(m_lock);
    Stats stats{};
    stats.chunksLive    = m_chunksLive;
    stats.chunksPooled  = m_pooledChunks.size();
    stats.blockChunks   = m_blockChunks.size();
    stats.largeBlocks   = m_largeBlocks.size();
    stats.bytesReserved = m_bytesReserved;
    return stats;
}

} // namespace Engine::Memory
