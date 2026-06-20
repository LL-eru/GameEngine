#pragma once

#include "Debugger.hxx"
#include "HostContext.hxx"
#include <type_traits>

#ifndef SHIPPING

#ifdef GE_PLUGIN
#define DebugString_(str) do { \
    HostServices* _hs = GetHostServices(); \
    if (_hs && _hs->DebugOutput) _hs->DebugOutput(str); \
} while(0)
#define DebugCOUT_(str) DebugString_(str)
#else
#define DebugString_(str) DebugStringOutput(str);
#define DebugCOUT_(str) DebugStringOutput(str);
#endif

#ifdef DEBUG

#ifdef GE_PLUGIN
#define _ENGINE_ASSERT_FAILED(exprStr) do { \
    HostServices* _hs = GetHostServices(); \
    if (_hs && _hs->Assert) _hs->Assert((exprStr), false); \
} while(0)
#else
#define _ENGINE_ASSERT_FAILED(exprStr) do { \
    (void)DebugAssert((exprStr), false); \
} while(0)
#endif

#define Check(value) _engine_check((value), #value)

template <typename T>
T& _engine_check(const T& value, const char* exprStr) {
    if constexpr (std::is_same_v<T, bool>) {
        if (!value) {
            _ENGINE_ASSERT_FAILED(exprStr);
            DebugBreakPoint_
        }
        return const_cast<T&>(value);
    }
    else if constexpr (std::is_pointer_v<T>) {
        if (value == nullptr) {
            _ENGINE_ASSERT_FAILED(exprStr);
            DebugBreakPoint_
        }
        return const_cast<T&>(value);
    }
    else if constexpr (std::is_same_v<T, HRESULT>) {
        if (FAILED(value)) {
            _ENGINE_ASSERT_FAILED(exprStr);
            DebugBreakPoint_
        }
        return const_cast<T&>(value);
    }
    else {
        _ENGINE_ASSERT_FAILED(exprStr);
        DebugBreakPoint_
        static T dummy{};
        return dummy;
    }
}

#else // !DEBUG
#define Check(value) (value)
#endif // DEBUG

#else // SHIPPING
#define DebugString_(str);
#define DebugCOUT_(str);
#define Check(value) (value)
#endif // !SHIPPING

#ifndef DEBUG
#define Check(value) (value)
#endif
