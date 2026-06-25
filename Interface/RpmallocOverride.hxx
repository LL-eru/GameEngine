#pragma once

// =============================================================================
// RpmallocOverride.hxx
//
// Include this header from EXACTLY ONE translation unit in a module (EXE or DLL)
// to route that module's C++ operator new / delete to the single rpmalloc heap
// that lives in Core.dll. Because every module forwards to the one rpmalloc
// instance (its rp* entry points are exported from Core, see Core/Rpmalloc.def),
// memory allocated in one module can be freed in another and on any thread.
//
// rpmalloc lazily initializes each thread's cache on first use, and Core.dll's
// DllMain initializes the process, so no explicit per-thread init is needed
// here. Including this in more than one TU per module is an ODR violation.
//
// Note: on the dynamic CRT (/MD) the C `malloc`/`free` symbols cannot be cleanly
// replaced per-module; they are fully overridden only inside Core.dll. Engine
// code should allocate via `operator new` or Engine::Allocate to use the shared
// heap. Third-party code calling raw `malloc` keeps using the CRT heap, which is
// still freed correctly by that same CRT.
// =============================================================================

#include "rpmalloc.h"
#include "rpnew.h" // defines operator new/delete -> rpmalloc/rpfree (imported)

namespace Engine::Detail {
// Touching rpmalloc_linker_reference keeps the override object alive under
// aggressive linkers and confirms the imported rp* symbols resolve.
inline void RpmallocOverrideAnchor() noexcept { rpmalloc_linker_reference(); }
} // namespace Engine::Detail
