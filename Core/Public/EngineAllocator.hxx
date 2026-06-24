#pragma once

#include "CoreExport.hxx"
#include "../../Interface/HostServices.hxx"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class GE_API FrameArena {
public:
    void Initialize(size_t capacityBytes);
    void Shutdown();
    void Reset();
    void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t));
    size_t GetUsedBytes() const { return m_offset; }

private:
    std::vector<unsigned char> m_buffer;
    size_t m_offset = 0;
};

class GE_API ObjectPool {
public:
    void Initialize(size_t objectSize, size_t capacity);
    void Shutdown();
    void* Allocate();
    void Free(void* ptr);
    bool Contains(const void* ptr) const;

private:
    size_t m_objectSize = 0;
    std::vector<unsigned char> m_storage;
    std::vector<void*> m_freeList;
};

class GE_API SegregatedFreeList {
public:
    void Initialize();
    void Shutdown();
    void* Allocate(size_t size);
    void Free(void* ptr, size_t size);

private:
    static constexpr size_t kBucketCount = 8;
    static constexpr size_t kBucketSizes[kBucketCount] = { 16, 32, 64, 128, 256, 512, 1024, 2048 };
    ObjectPool m_buckets[kBucketCount];
    std::vector<std::pair<void*, size_t>> m_largeBlocks;
};

class GE_API GPUArena {
public:
    void Initialize(size_t capacityBytes);
    void Shutdown();
    void Reset();
    void* Allocate(size_t size, size_t alignment = 256);

private:
    FrameArena m_arena;
};

class GE_API EngineAllocator {
public:
    static void Initialize();
    static void Shutdown();
    static void* Alloc(size_t size, uint32_t arenaId);
    static void Free(void* ptr, uint32_t arenaId);
    static void FrameArenaReset();

    static FrameArena& GetFrameArena();
    static ObjectPool& GetObjectPool();
    static SegregatedFreeList& GetSegregatedFreeList();
    static GPUArena& GetGPUArena();

private:
    static std::unique_ptr<FrameArena> s_frameArena;
    static std::unique_ptr<ObjectPool> s_objectPool;
    static std::unique_ptr<SegregatedFreeList> s_segregated;
    static std::unique_ptr<GPUArena> s_gpuArena;
};

GE_API void* CoreAlloc(size_t size, uint32_t arenaId);
GE_API void CoreFree(void* ptr, uint32_t arenaId);
GE_API void CoreFrameArenaReset();
