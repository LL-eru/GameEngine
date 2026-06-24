#include "../Interface/MemoryAPI.hxx"
#include "Public/ThreadLocalAllocator.hxx"

// Implements the module-boundary allocator entry points declared in
// Interface/MemoryAPI.hxx. Built into Core.dll (GE_BUILD_CORE), so ENGINE_API
// resolves to dllexport here and dllimport for every other module.

namespace Engine {

void* Allocate(std::size_t size, std::size_t alignment) {
    if (size == 0) return nullptr;
    return Memory::ThreadCache::Get().Allocate(size, alignment);
}

void Free(void* ptr) noexcept {
    if (ptr == nullptr) return;
    Memory::ThreadCache::Get().Free(ptr);
}

void FlushThreadCache() noexcept {
    Memory::ThreadCache::Get().Flush();
}

MemoryStatsView QueryMemoryStats() noexcept {
    const auto stats = Memory::GlobalCentralManager().GetStats();
    MemoryStatsView view{};
    view.blockChunks   = stats.blockChunks;
    view.largeBlocks   = stats.largeBlocks;
    view.bytesReserved = stats.bytesReserved;
    return view;
}

} // namespace Engine
