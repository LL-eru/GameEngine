// =============================================================================
// CrossModulePlugin.cxx
//
// A stand-in "plugin DLL" for the cross-module test. It links against Core.lib
// and forwards to the imported Engine::Allocate / Engine::Free, demonstrating
// that a *different* binary shares Core.dll's single allocator instance.
// =============================================================================

#include "MemoryAPI.hxx" // Engine::Allocate / Free (dllimport from Core)

#include <cstddef>

extern "C" {

__declspec(dllexport) void* PluginAllocate(std::size_t size, std::size_t alignment) {
    return Engine::Allocate(size, alignment);
}

__declspec(dllexport) void PluginFree(void* ptr) {
    Engine::Free(ptr);
}

} // extern "C"
