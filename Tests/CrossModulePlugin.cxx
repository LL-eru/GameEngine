// =============================================================================
// CrossModulePlugin.cxx
//
// A stand-in "plugin DLL" for the cross-module test. It links against Core.lib
// and shares Core.dll's single rpmalloc instance two ways:
//   * via the exported Engine::Allocate / Engine::Free API, and
//   * via its own operator new / delete, which are overridden (rpnew.h) to
//     forward to the imported rpmalloc/rpfree.
// This proves a *different* binary allocates from / frees to the one heap.
// =============================================================================

#include "MemoryAPI.hxx"        // Engine::Allocate / Free (dllimport from Core)
#include "RpmallocOverride.hxx" // overrides this module's operator new/delete

#include <cstddef>
#include <new>

extern "C" {

__declspec(dllexport) void* PluginAllocate(std::size_t size, std::size_t alignment) {
    return Engine::Allocate(size, alignment);
}

__declspec(dllexport) void PluginFree(void* ptr) {
    Engine::Free(ptr);
}

// Raw operator new / delete performed *inside this module*. Because new/delete
// are overridden here, the returned block lives in Core's shared rpmalloc heap
// and can be released by operator delete in any other module.
__declspec(dllexport) void* PluginOperatorNew(std::size_t size) {
    return ::operator new(size);
}

__declspec(dllexport) void PluginOperatorDelete(void* ptr) {
    ::operator delete(ptr);
}

} // extern "C"
