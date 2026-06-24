#pragma once

// =============================================================================
// MemoryAPI.hxx  (Interface)
//
// Phase 4: Global, module-boundary allocation interface.
//
//   * ENGINE_API marks the symbols that cross the EXE/DLL boundary. The owning
//     module (Core) is compiled with GE_BUILD_CORE and exports them; every
//     other module imports them. A single allocator instance therefore lives in
//     Core.dll and is shared by the EXE and all plugin DLLs.
//   * Engine::Allocate / Engine::Free are the only entry points modules need.
//     Because all calls funnel into Core's one CentralMemoryManager + thread
//     caches, memory allocated in one module can be freed in another (and on a
//     different thread) safely -- the central goal of this subsystem.
//
// The interface is a flat C-ABI-friendly surface (size_t / void*), so no STL
// types leak across the boundary.
// =============================================================================

#include <cstddef>

#if defined(_WIN32)
#  if defined(GE_BUILD_CORE)
#    define ENGINE_API __declspec(dllexport)
#  else
#    define ENGINE_API __declspec(dllimport)
#  endif
#else
#  if defined(GE_BUILD_CORE)
#    define ENGINE_API __attribute__((visibility("default")))
#  else
#    define ENGINE_API
#  endif
#endif

namespace Engine {

// Allocate `size` bytes aligned to at least `alignment` (a power of two).
// Returns nullptr for a zero-byte request or on out-of-memory.
[[nodiscard]] ENGINE_API void* Allocate(std::size_t size,
                                        std::size_t alignment = alignof(std::max_align_t));

// Free a pointer previously returned by Engine::Allocate. May be called from any
// thread or module, not just the one that allocated it. Null is ignored.
ENGINE_API void Free(void* ptr) noexcept;

// Manually flush the calling thread's cache back to the central manager. Called
// automatically when a thread exits; exposed for pools/tests that want eager
// reclamation.
ENGINE_API void FlushThreadCache() noexcept;

// Lightweight diagnostics snapshot (POD, ABI-stable).
struct MemoryStatsView {
    std::size_t blockChunks   = 0;
    std::size_t largeBlocks   = 0;
    std::size_t bytesReserved = 0;
};
[[nodiscard]] ENGINE_API MemoryStatsView QueryMemoryStats() noexcept;

} // namespace Engine
