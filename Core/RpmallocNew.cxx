// Overrides Core.dll's own C++ operator new/delete so they route to the
// rpmalloc instance compiled into this module (see Rpmalloc.def for the C
// entry points exported to other modules). rpmalloc.c (compiled with
// ENABLE_OVERRIDE) already replaces the C malloc family and installs the
// auto-init DllMain for this DLL.
//
// rpnew.h must be included from exactly one translation unit per module.

#include "rpmalloc.h"
#include "rpnew.h"

namespace {
// Referencing rpmalloc_linker_reference guarantees the override object files
// are pulled in by the linker even when nothing else touches them directly.
struct RpmallocLinkerAnchor {
    RpmallocLinkerAnchor() { rpmalloc_linker_reference(); }
};
const RpmallocLinkerAnchor g_rpmallocLinkerAnchor;
} // namespace
