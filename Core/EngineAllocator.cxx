#include "Public/EngineAllocator.hxx"
#include <algorithm>
#include <cstring>
#include <cstdlib>

std::unique_ptr<FrameArena> EngineAllocator::s_frameArena;
std::unique_ptr<ObjectPool> EngineAllocator::s_objectPool;
std::unique_ptr<SegregatedFreeList> EngineAllocator::s_segregated;
std::unique_ptr<GPUArena> EngineAllocator::s_gpuArena;

void FrameArena::Initialize(size_t capacityBytes) {
    m_buffer.resize(capacityBytes);
    m_offset = 0;
}

void FrameArena::Shutdown() {
    m_buffer.clear();
    m_offset = 0;
}

void FrameArena::Reset() {
    m_offset = 0;
}

void* FrameArena::Allocate(size_t size, size_t alignment) {
    if (size == 0) return nullptr;
    size_t aligned = (m_offset + alignment - 1) & ~(alignment - 1);
    if (aligned + size > m_buffer.size()) return nullptr;
    void* ptr = m_buffer.data() + aligned;
    m_offset = aligned + size;
    return ptr;
}

void ObjectPool::Initialize(size_t objectSize, size_t capacity) {
    m_objectSize = std::max(objectSize, sizeof(void*));
    m_storage.resize(m_objectSize * capacity);
    m_freeList.clear();
    m_freeList.reserve(capacity);
    for (size_t i = 0; i < capacity; ++i) {
        m_freeList.push_back(m_storage.data() + i * m_objectSize);
    }
}

void ObjectPool::Shutdown() {
    m_storage.clear();
    m_freeList.clear();
    m_objectSize = 0;
}

void* ObjectPool::Allocate() {
    if (m_freeList.empty()) return nullptr;
    void* ptr = m_freeList.back();
    m_freeList.pop_back();
    return ptr;
}

void ObjectPool::Free(void* ptr) {
    if (!ptr) return;
    m_freeList.push_back(ptr);
}

bool ObjectPool::Contains(const void* ptr) const {
    if (!ptr || m_storage.empty()) return false;
    const auto* begin = m_storage.data();
    const auto* end = begin + m_storage.size();
    const auto* p = static_cast<const unsigned char*>(ptr);
    return p >= begin && p < end;
}

void SegregatedFreeList::Initialize() {
    for (size_t i = 0; i < kBucketCount; ++i) {
        m_buckets[i].Initialize(kBucketSizes[i], 64);
    }
    m_largeBlocks.clear();
}

void SegregatedFreeList::Shutdown() {
    for (auto& block : m_largeBlocks) {
        std::free(block.first);
    }
    m_largeBlocks.clear();
    for (size_t i = 0; i < kBucketCount; ++i) {
        m_buckets[i].Shutdown();
    }
}

void* SegregatedFreeList::Allocate(size_t size) {
    for (size_t i = 0; i < kBucketCount; ++i) {
        if (size <= kBucketSizes[i]) {
            if (void* ptr = m_buckets[i].Allocate()) return ptr;
            break;
        }
    }
    void* ptr = std::malloc(size);
    if (ptr) m_largeBlocks.emplace_back(ptr, size);
    return ptr;
}

void SegregatedFreeList::Free(void* ptr, size_t /*size*/) {
    if (!ptr) return;
    for (size_t i = 0; i < kBucketCount; ++i) {
        if (m_buckets[i].Contains(ptr)) {
            m_buckets[i].Free(ptr);
            return;
        }
    }
    auto it = std::find_if(m_largeBlocks.begin(), m_largeBlocks.end(),
        [ptr](const auto& entry) { return entry.first == ptr; });
    if (it != m_largeBlocks.end()) {
        std::free(it->first);
        m_largeBlocks.erase(it);
    }
}

void GPUArena::Initialize(size_t capacityBytes) {
    m_arena.Initialize(capacityBytes);
}

void GPUArena::Shutdown() {
    m_arena.Shutdown();
}

void GPUArena::Reset() {
    m_arena.Reset();
}

void* GPUArena::Allocate(size_t size, size_t alignment) {
    return m_arena.Allocate(size, alignment);
}

void EngineAllocator::Initialize() {
    s_frameArena = std::make_unique<FrameArena>();
    s_objectPool = std::make_unique<ObjectPool>();
    s_segregated = std::make_unique<SegregatedFreeList>();
    s_gpuArena = std::make_unique<GPUArena>();

    s_frameArena->Initialize(16 * 1024 * 1024);
    s_objectPool->Initialize(64, 1024);
    s_segregated->Initialize();
    s_gpuArena->Initialize(64 * 1024 * 1024);
}

void EngineAllocator::Shutdown() {
    if (s_gpuArena) s_gpuArena->Shutdown();
    if (s_segregated) s_segregated->Shutdown();
    if (s_objectPool) s_objectPool->Shutdown();
    if (s_frameArena) s_frameArena->Shutdown();
    s_gpuArena.reset();
    s_segregated.reset();
    s_objectPool.reset();
    s_frameArena.reset();
}

void* EngineAllocator::Alloc(size_t size, uint32_t arenaId) {
    switch (static_cast<ArenaId>(arenaId)) {
    case ArenaId::Frame:
        return s_frameArena ? s_frameArena->Allocate(size) : nullptr;
    case ArenaId::ObjectPool:
        return s_objectPool ? s_objectPool->Allocate() : nullptr;
    case ArenaId::Segregated:
        return s_segregated ? s_segregated->Allocate(size) : nullptr;
    case ArenaId::GPU:
        return s_gpuArena ? s_gpuArena->Allocate(size) : nullptr;
    default:
        return nullptr;
    }
}

void EngineAllocator::Free(void* ptr, uint32_t arenaId) {
    if (!ptr) return;
    switch (static_cast<ArenaId>(arenaId)) {
    case ArenaId::ObjectPool:
        if (s_objectPool) s_objectPool->Free(ptr);
        break;
    case ArenaId::Segregated:
        if (s_segregated) s_segregated->Free(ptr, 0);
        break;
    default:
        break;
    }
}

void EngineAllocator::FrameArenaReset() {
    if (s_frameArena) s_frameArena->Reset();
    if (s_gpuArena) s_gpuArena->Reset();
}

FrameArena& EngineAllocator::GetFrameArena() { return *s_frameArena; }
ObjectPool& EngineAllocator::GetObjectPool() { return *s_objectPool; }
SegregatedFreeList& EngineAllocator::GetSegregatedFreeList() { return *s_segregated; }
GPUArena& EngineAllocator::GetGPUArena() { return *s_gpuArena; }

void* CoreAlloc(size_t size, uint32_t arenaId) {
    return EngineAllocator::Alloc(size, arenaId);
}

void CoreFree(void* ptr, uint32_t arenaId) {
    EngineAllocator::Free(ptr, arenaId);
}

void CoreFrameArenaReset() {
    EngineAllocator::FrameArenaReset();
}
