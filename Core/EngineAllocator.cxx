#include "Public/EngineAllocator.hxx"
#include "Public/EngineVirtualMemory.hxx"

#include "rpmalloc.h"

#include <algorithm>

using Engine::Memory::IsPowerOfTwo;

std::unique_ptr<FrameArena> EngineAllocator::s_frameArena;
std::unique_ptr<GPUArena>   EngineAllocator::s_gpuArena;
std::vector<std::unique_ptr<ObjectPool_T>> EngineAllocator::s_pools;

void EngineAllocator::Initialize(const AllocatorConfig& config) {
    s_frameArena = std::make_unique<FrameArena>();
    s_gpuArena   = std::make_unique<GPUArena>();

    s_frameArena->Initialize(config.frameArenaCapacityBytes);
    s_gpuArena->Initialize(config.gpuArenaCapacityBytes);
}

void EngineAllocator::Shutdown() {
    s_pools.clear();
    if (s_gpuArena) s_gpuArena->Shutdown();
    if (s_frameArena) s_frameArena->Shutdown();
    s_gpuArena.reset();
    s_frameArena.reset();
}

void* EngineAllocator::AllocHeap(size_t size, size_t alignment) {
    if (size == 0) return nullptr;
    if (alignment == 0) alignment = alignof(std::max_align_t);
    ENGINE_VM_VERIFY(IsPowerOfTwo(alignment),
                     "EngineAllocator::AllocHeap alignment must be a power of two");
    if (!IsPowerOfTwo(alignment)) return nullptr;
    return rpaligned_alloc(alignment, size);
}

void EngineAllocator::FreeHeap(void* ptr) {
    if (!ptr) return;
    rpfree(ptr);
}

void* EngineAllocator::AllocFrame(size_t size, size_t alignment) {
    if (alignment == 0) alignment = alignof(std::max_align_t);
    ENGINE_VM_VERIFY(IsPowerOfTwo(alignment),
                     "EngineAllocator::AllocFrame alignment must be a power of two");
    if (!IsPowerOfTwo(alignment)) return nullptr;
    return s_frameArena ? s_frameArena->Allocate(size, alignment) : nullptr;
}

void* EngineAllocator::AllocGpu(size_t size, size_t alignment) {
    if (alignment == 0) alignment = 256;
    ENGINE_VM_VERIFY(IsPowerOfTwo(alignment),
                     "EngineAllocator::AllocGpu alignment must be a power of two");
    if (!IsPowerOfTwo(alignment)) return nullptr;
    return s_gpuArena ? s_gpuArena->Allocate(size, alignment) : nullptr;
}

void EngineAllocator::ResetFrameArenas() {
    if (s_frameArena) s_frameArena->Reset();
    if (s_gpuArena) s_gpuArena->Reset();
}

bool EngineAllocator::IsLivePool(PoolHandle pool) {
    if (pool == nullptr) return false;
    return std::any_of(s_pools.begin(), s_pools.end(),
                       [pool](const std::unique_ptr<ObjectPool_T>& entry) {
                           return entry.get() == pool;
                       });
}

PoolHandle EngineAllocator::CreatePool(size_t objectSize, size_t capacity) {
    if (objectSize == 0 || capacity == 0) return nullptr;

    auto wrapper = std::make_unique<ObjectPool_T>();
    wrapper->pool.Initialize(objectSize, capacity);
    PoolHandle handle = wrapper.get();
    s_pools.push_back(std::move(wrapper));
    return handle;
}

void EngineAllocator::DestroyPool(PoolHandle pool) {
    if (!IsLivePool(pool)) return;

    pool->pool.Shutdown();
    s_pools.erase(
        std::remove_if(s_pools.begin(), s_pools.end(),
                       [pool](const std::unique_ptr<ObjectPool_T>& entry) {
                           return entry.get() == pool;
                       }),
        s_pools.end());
}

void* EngineAllocator::AllocPool(PoolHandle pool) {
    if (!IsLivePool(pool)) return nullptr;
    return pool->pool.Allocate();
}

void EngineAllocator::FreePool(PoolHandle pool, void* ptr) {
    if (!ptr || !IsLivePool(pool)) return;
    pool->pool.Free(ptr);
}

FrameArena& EngineAllocator::GetFrameArena() {
    ENGINE_VM_VERIFY(s_frameArena != nullptr,
                     "GetFrameArena() called before Initialize() or after Shutdown()");
    return *s_frameArena;
}

GPUArena& EngineAllocator::GetGPUArena() {
    ENGINE_VM_VERIFY(s_gpuArena != nullptr,
                     "GetGPUArena() called before Initialize() or after Shutdown()");
    return *s_gpuArena;
}

void* CoreAllocHeap(size_t size, size_t alignment) {
    return EngineAllocator::AllocHeap(size, alignment);
}

void CoreFreeHeap(void* ptr) {
    EngineAllocator::FreeHeap(ptr);
}

void* CoreAllocFrame(size_t size, size_t alignment) {
    return EngineAllocator::AllocFrame(size, alignment);
}

void* CoreAllocGpu(size_t size, size_t alignment) {
    return EngineAllocator::AllocGpu(size, alignment);
}

PoolHandle CoreCreatePool(size_t objectSize, size_t capacity) {
    return EngineAllocator::CreatePool(objectSize, capacity);
}

void CoreDestroyPool(PoolHandle pool) {
    EngineAllocator::DestroyPool(pool);
}

void* CoreAllocPool(PoolHandle pool) {
    return EngineAllocator::AllocPool(pool);
}

void CoreFreePool(PoolHandle pool, void* ptr) {
    EngineAllocator::FreePool(pool, ptr);
}

void CoreResetFrameArenas() {
    EngineAllocator::ResetFrameArenas();
}
