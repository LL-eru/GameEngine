#pragma once

#include <cstddef>

// Log levels match Core::LogLevel ordinal values.
enum class HostLogLevel : int {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Fatal = 5
};

// Opaque pool handle: plugins see only a pointer tag, never ObjectPool internals.
struct ObjectPool_T;
using PoolHandle = ObjectPool_T*;

// Ownership across module boundaries:
//   * Memory from Engine::Allocate/Free or HostServices::AllocHeap/FreeHeap must
//     be released through the same engine API (or operator new/delete overridden
//     via RpmallocOverride.hxx). Never pass engine-owned pointers to third-party
//     code that will call raw malloc/free on them.
//   * Third-party libraries (Qt, GLFW, Åc) keep their own CRT heap; treat their
//     allocations as foreign unless you copy data into engine-owned memory.
//
// Threading:
//   * AllocHeap / FreeHeap (rpmalloc) is thread-safe.
//   * Frame / GPU arenas and object pools are single-thread; use one arena or
//     pool per worker when a job system is introduced.
struct HostServices {
    void (*Log)(HostLogLevel level, const char* category, const char* message);
    void (*DebugOutput)(const char* text);
    bool (*Assert)(const char* expr, bool condition);

    // --- General-purpose heap (rpmalloc; free from any module / thread) ---
    // `alignment` must be a power of two (pass alignof(max_align_t) for default).
    void* (*AllocHeap)(size_t size, size_t alignment);
    void  (*FreeHeap)(void* ptr);

    // --- Frame scratch (no per-pointer free; reclaimed by ResetFrameArenas) ---
    void* (*AllocFrame)(size_t size, size_t alignment);

    // --- GPU upload staging (256 B default alignment inside Core; no per-pointer free) ---
    void* (*AllocGpu)(size_t size, size_t alignment);

    void (*ResetFrameArenas)();

    // --- Fixed-size object pools (size and capacity fixed at CreatePool time) ---
    PoolHandle (*CreatePool)(size_t objectSize, size_t capacity);
    void       (*DestroyPool)(PoolHandle pool);
    void*      (*AllocPool)(PoolHandle pool);
    void       (*FreePool)(PoolHandle pool, void* ptr);
};

using GetHostServicesFn = HostServices* (*)();
