#include "Public/ThreadLocalAllocator.hxx"

#include <cstring>

namespace Engine::Memory {

CentralMemoryManager& GlobalCentralManager() noexcept {
    // Leaky singleton: constructed on first use, never destroyed. The OS
    // reclaims the address space at process exit.
    static CentralMemoryManager* instance = [] {
        auto* p = new CentralMemoryManager();
        p->Initialize();
        return p;
    }();
    return *instance;
}

ThreadCache& ThreadCache::Get() noexcept {
    thread_local ThreadCache cache;
    return cache;
}

ThreadCache::~ThreadCache() {
    Flush();
}

// Intrusive free-list helpers. The first pointer-sized word of a cached block
// stores the link; it is the one region PoisonFreeBlock leaves accessible.
namespace {
inline void* ReadLink(void* block) noexcept {
    void* next = nullptr;
    std::memcpy(&next, block, sizeof(next));
    return next;
}

inline void WriteLink(void* block, void* next) noexcept {
    std::memcpy(block, &next, sizeof(next));
}
} // namespace

void* ThreadCache::Allocate(std::size_t size, std::size_t alignment) {
    if (size == 0) return nullptr;

    const std::size_t blockSize = NormalizedBlockSize(size, alignment);
    if (blockSize > kMaxBlockSize) {
        // Oversized requests skip the cache entirely.
        return GlobalCentralManager().AllocateLarge(size, alignment);
    }

    const unsigned sizeClass = SizeClassIndex(blockSize);
    FreeList& list = m_lists[sizeClass];

    if (list.head == nullptr) {
        void* batch[kRefillBatch];
        const std::uint32_t got =
            GlobalCentralManager().AcquireBlocks(sizeClass, batch, kRefillBatch);
        if (got == 0) return nullptr; // OOM
        for (std::uint32_t i = 0; i < got; ++i) {
            WriteLink(batch[i], list.head);
            list.head = batch[i];
        }
        list.count += got;
    }

    void* block = list.head;
    list.head = ReadLink(block);
    list.count -= 1;

    ENGINE_ASAN_UNPOISON(block, blockSize);
    return block;
}

void ThreadCache::Free(void* ptr) {
    if (ptr == nullptr) return;

    ChunkHeader* hdr = HeaderFromPointer(ptr, CentralMemoryManager::kChunkSize);
    ENGINE_VM_VERIFY(hdr->magic == kChunkMagic, "Free of a pointer the allocator never produced");

    if (hdr->kind == ChunkKind::Large) {
        GlobalCentralManager().FreeLarge(ptr);
        return;
    }

    const unsigned sizeClass = hdr->sizeClass;
    const std::size_t blockSize = hdr->blockSize;
    ENGINE_VM_VERIFY(sizeClass < kNumSizeClasses, "corrupt size class in chunk header");

    // Poison the payload (keep the link word usable) and push onto this thread's
    // free list. Note: this can be a block another thread allocated.
    if (blockSize > sizeof(void*)) {
        ENGINE_ASAN_POISON(static_cast<std::byte*>(ptr) + sizeof(void*),
                           blockSize - sizeof(void*));
    }
    FreeList& list = m_lists[sizeClass];
    WriteLink(ptr, list.head);
    list.head = ptr;
    list.count += 1;

    if (list.count > kMaxCachedPerClass) {
        SpillClass(sizeClass);
    }
}

void ThreadCache::SpillClass(unsigned sizeClass) {
    FreeList& list = m_lists[sizeClass];
    void* batch[kRefillBatch];
    std::uint32_t n = 0;
    while (n < kRefillBatch && list.head != nullptr) {
        void* block = list.head;
        list.head = ReadLink(block);
        list.count -= 1;
        batch[n++] = block;
    }
    if (n > 0) {
        GlobalCentralManager().ReleaseBlocks(batch, n);
    }
}

void ThreadCache::Flush() {
    CentralMemoryManager& central = GlobalCentralManager();
    void* batch[kRefillBatch];

    for (unsigned cls = 0; cls < kNumSizeClasses; ++cls) {
        FreeList& list = m_lists[cls];
        while (list.head != nullptr) {
            std::uint32_t n = 0;
            while (n < kRefillBatch && list.head != nullptr) {
                void* block = list.head;
                list.head = ReadLink(block);
                list.count -= 1;
                batch[n++] = block;
            }
            central.ReleaseBlocks(batch, n);
        }
        list.count = 0;
    }
}

ThreadCache::Stats ThreadCache::GetStats() const noexcept {
    Stats stats{};
    for (unsigned cls = 0; cls < kNumSizeClasses; ++cls) {
        const std::uint32_t count = m_lists[cls].count;
        stats.cachedBlocks += count;
        stats.cachedBytes += static_cast<std::size_t>(count) * BlockSizeForClass(cls);
    }
    return stats;
}

} // namespace Engine::Memory
