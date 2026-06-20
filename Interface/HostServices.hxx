#pragma once

#include <cstddef>
#include <cstdint>

// Log levels match Core::LogLevel ordinal values.
enum class HostLogLevel : int {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Fatal = 5
};

enum class ArenaId : uint32_t {
    Frame      = 0,
    ObjectPool = 1,
    Segregated = 2,
    GPU        = 3,
    Count
};

struct HostServices {
    void (*Log)(HostLogLevel level, const char* category, const char* message);
    void (*DebugOutput)(const char* text);
    bool (*Assert)(const char* expr, bool condition);
    void* (*Alloc)(size_t size, uint32_t arenaId);
    void  (*Free)(void* ptr, uint32_t arenaId);
    void  (*FrameArenaReset)();
};

using GetHostServicesFn = HostServices* (*)();
