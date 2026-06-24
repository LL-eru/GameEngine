#pragma once

#include "CoreExport.hxx"

#ifndef SHIPPING
#include <debugging>

GE_API void DebugStringOutput(const char* text);
GE_API bool DebugAssert(const char* expr, bool condition);

#ifdef DEBUG
#define DebugBreakPoint_ std::breakpoint_if_debugging();
#else
#define DebugBreakPoint_
#endif

#else // SHIPPING
#define DebugBreakPoint_
#endif
