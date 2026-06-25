#include "../Interface/MemoryAPI.hxx"

#include "Public/EngineVirtualMemory.hxx"
#include "rpmalloc.h"

using Engine::Memory::IsPowerOfTwo;

// Implements the module-boundary allocator entry points declared in
// Interface/MemoryAPI.hxx. Built into Core.dll (GE_BUILD_CORE), so ENGINE_API
// resolves to dllexport here and dllimport for every other module. All calls
// funnel into the single rpmalloc instance hosted by Core.dll, so memory
// allocated in one module can be freed in another and on any thread.

namespace Engine {

void* Allocate(std::size_t size, std::size_t alignment) {
    if (size == 0) return nullptr;
    if (alignment == 0) alignment = alignof(std::max_align_t);
    ENGINE_VM_VERIFY(IsPowerOfTwo(alignment),
                     "Engine::Allocate alignment must be a power of two");
    if (!IsPowerOfTwo(alignment)) return nullptr;
    return rpaligned_alloc(alignment, size);
}

void Free(void* ptr) noexcept {
    if (ptr == nullptr) return;
    rpfree(ptr);
}

void FlushThreadCache() noexcept {
    // Return this thread's cached spans to the global cache.
    rpmalloc_thread_collect();
}

MemoryStatsView QueryMemoryStats() noexcept {
    rpmalloc_global_statistics_t stats{};
    rpmalloc_global_statistics(&stats);

    MemoryStatsView view{};
    // Map rpmalloc's global byte counters onto the ABI-stable view. Core.dll
    // compiles rpmalloc with ENABLE_STATISTICS, so all three are populated.
    view.bytesMapped    = stats.mapped;
    view.bytesCached    = stats.cached;
    view.bytesHugeAlloc = stats.huge_alloc;
    return view;
}

} // namespace Engine
