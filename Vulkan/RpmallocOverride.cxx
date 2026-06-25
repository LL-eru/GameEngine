// Routes this module's operator new/delete to the shared rpmalloc heap in
// Core.dll. See Interface/RpmallocOverride.hxx. Must be the only TU in this
// module that includes that header.
#include "RpmallocOverride.hxx"
