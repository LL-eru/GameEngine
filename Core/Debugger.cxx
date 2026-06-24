#include "Public/Debugger.hxx"

#ifndef SHIPPING
#include <windows.h>
#include <debugging>
#include <iostream>

class DebugConsoleHolder {
public:
    ~DebugConsoleHolder() {
#ifndef DEBUG
        rewind(stdin);
        (void)getchar();
#endif
    }
};

void DebugStringOutput(const char* text) {
    static bool initialized = true;
    static DebugConsoleHolder holder;
    if (initialized) {
        AllocConsole();
#pragma warning(push)
#pragma warning(disable: 4996)
        (void)freopen("CON", "r", stdin);
        (void)freopen("CON", "w", stdout);
#pragma warning(pop)
        initialized = false;
    }
    if (text) {
        std::cout << text;
    }
}

bool DebugAssert(const char* expr, bool condition) {
    if (condition) return true;
    DebugStringOutput("Check failed: ");
    DebugStringOutput(expr ? expr : "(null)");
    DebugStringOutput("\n");
    std::breakpoint_if_debugging();
    return false;
}

#else

void DebugStringOutput(const char*) {}
bool DebugAssert(const char*, bool condition) { return condition; }

#endif
