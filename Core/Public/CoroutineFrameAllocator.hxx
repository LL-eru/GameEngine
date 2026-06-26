#pragma once

#include "CoreExport.hxx"

#include <cstddef>

namespace Engine::detail {

// Pool-backed allocator for coroutine frames (promise_type::operator new/delete).
// Backed by a static pmr buffer in Core.dll; falls back to rpmalloc for oversized frames.
class GE_API CoroutineFrameAllocator {
public:
    static void* Allocate(std::size_t size);
    static void Deallocate(void* ptr, std::size_t size) noexcept;

    [[nodiscard]] static std::size_t LiveAllocations() noexcept;
    [[nodiscard]] static std::size_t TotalBytesAllocated() noexcept;

    static void ResetPeakStats() noexcept;
};

} // namespace Engine::detail
