#pragma once

#include "CoreExport.hxx"

#ifndef SHIPPING
#include <windows.h>
#include <string>
#include <type_traits>

GE_API void DebugStringOutput(const char* text);
GE_API bool DebugAssert(const char* expr, bool condition);

#ifdef DEBUG
#define DebugBreakPoint_ DebugBreak();
#else
#define DebugBreakPoint_
#endif

#else // SHIPPING
#define DebugBreakPoint_
#endif
