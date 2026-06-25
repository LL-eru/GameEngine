#pragma once

// =============================================================================
// EngineAllocator.hxx  (Core-internal)
//
// FrameArena, GPUArena, ObjectPool, and EngineAllocator live here. They are
// deliberately NOT marked GE_API: they contain STL members and must never cross
// the DLL boundary as C++ types.
//
// Plugin and host modules must use the stable surfaces instead:
//   * Interface/MemoryAPI.hxx     (Engine::Allocate / Free / ...)
//   * Interface/HostServices.hxx  (AllocHeap, AllocFrame, CreatePool, ...)
//
// Include this header only from Core translation units and in-engine tests.
// =============================================================================

#include "../../Interface/HostServices.hxx"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Three-tier allocation surface:
//
//   * FrameArena / GPUArena : OS-direct bump allocators (single-thread).
//   * ObjectPool            : fixed-size free list (single-thread), managed via
//                             PoolHandle through HostServices::CreatePool.
//   * AllocHeap             : general-purpose heap via Core.dll rpmalloc.
//
// Future: when a job system lands, give each worker its own FrameArena rather
// than adding locks to the global scratch arenas (lock-free per-thread scratch).

struct AllocatorConfig {
    size_t frameArenaCapacityBytes = 16 * 1024 * 1024;
    size_t gpuArenaCapacityBytes   = 64 * 1024 * 1024;
};

// -----------------------------------------------------------------------------
// FrameArena: OS-direct bump allocator for short-lived (per-frame) data.
//
// THREAD SAFETY: NOT thread-safe. One owner thread per instance. When worker
// threads need scratch memory, allocate one FrameArena per thread.
// -----------------------------------------------------------------------------
class FrameArena {
public:
    void Initialize(size_t capacityBytes);
    void Shutdown();
    void Reset();
    void* Allocate(size_t size, size_t alignment = alignof(std::max_align_t));

    size_t GetUsedBytes() const { return m_offset; }
    size_t GetCapacityBytes() const { return m_capacity; }

private:
    unsigned char* m_base = nullptr;
    size_t         m_capacity = 0;
    size_t         m_offset = 0;
};

// -----------------------------------------------------------------------------
// GPUArena: FrameArena tuned for GPU upload staging (256 B default align).
// CPU-visible memory; not a GPU heap.
// THREAD SAFETY: NOT thread-safe (inherits FrameArena's contract).
// -----------------------------------------------------------------------------
class GPUArena {
public:
    void Initialize(size_t capacityBytes);
    void Shutdown();
    void Reset();
    void* Allocate(size_t size, size_t alignment = 256);

    size_t GetUsedBytes() const { return m_arena.GetUsedBytes(); }
    size_t GetCapacityBytes() const { return m_arena.GetCapacityBytes(); }

private:
    FrameArena m_arena;
};

// -----------------------------------------------------------------------------
// ObjectPool: fixed-size intrusive free list with occupancy bitmap.
// THREAD SAFETY: NOT thread-safe. Use one pool per owner thread or serialise.
// -----------------------------------------------------------------------------
class ObjectPool {
public:
    void Initialize(size_t objectSize, size_t capacity,
                    size_t alignment = alignof(std::max_align_t));
    void Shutdown();
    void* Allocate();
    void Free(void* ptr);
    bool Contains(const void* ptr) const;

    size_t GetObjectSize() const { return m_objectSize; }
    size_t GetCapacity() const { return m_capacity; }
    size_t GetFreeCount() const { return m_freeCount; }

private:
    size_t IndexOf(const void* ptr) const;
    bool TestBit(size_t index) const;
    void SetBit(size_t index);
    void ClearBit(size_t index);

    size_t m_objectSize = 0;
    size_t m_alignment = 0;
    size_t m_capacity = 0;
    size_t m_freeCount = 0;
    unsigned char* m_base = nullptr;
    void* m_freeHead = nullptr;
    std::vector<unsigned char> m_storage;
    std::vector<uint64_t> m_inUse;
};

// Opaque wrapper: PoolHandle in HostServices.hxx points at this struct.
struct ObjectPool_T {
    ObjectPool pool;
};

// -----------------------------------------------------------------------------
// EngineAllocator: singleton wired into CoreInit / HostServices bindings.
// -----------------------------------------------------------------------------
class EngineAllocator {
public:
    static void Initialize(const AllocatorConfig& config = {});
    static void Shutdown();

    static void* AllocHeap(size_t size, size_t alignment);
    static void  FreeHeap(void* ptr);
    static void* AllocFrame(size_t size, size_t alignment);
    static void* AllocGpu(size_t size, size_t alignment);
    static void  ResetFrameArenas();

    static PoolHandle CreatePool(size_t objectSize, size_t capacity);
    static void       DestroyPool(PoolHandle pool);
    static void*      AllocPool(PoolHandle pool);
    static void       FreePool(PoolHandle pool, void* ptr);

    static FrameArena& GetFrameArena();
    static GPUArena&   GetGPUArena();

private:
    static bool IsLivePool(PoolHandle pool);

    static std::unique_ptr<FrameArena> s_frameArena;
    static std::unique_ptr<GPUArena>   s_gpuArena;
    static std::vector<std::unique_ptr<ObjectPool_T>> s_pools;
};

// HostServices bindings (Core.dll only; reached via function pointers, not export).
void*      CoreAllocHeap(size_t size, size_t alignment);
void       CoreFreeHeap(void* ptr);
void*      CoreAllocFrame(size_t size, size_t alignment);
void*      CoreAllocGpu(size_t size, size_t alignment);
PoolHandle CoreCreatePool(size_t objectSize, size_t capacity);
void       CoreDestroyPool(PoolHandle pool);
void*      CoreAllocPool(PoolHandle pool);
void       CoreFreePool(PoolHandle pool, void* ptr);
void       CoreResetFrameArenas();
