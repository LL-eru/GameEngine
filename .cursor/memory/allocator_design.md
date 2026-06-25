# Allocator Design Memory

The engine uses a three-tier allocation model.

## Stable DLL boundary (plugins / hosts)

Cross-module code must use only:

- `Interface/MemoryAPI.hxx` - `Engine::Allocate`, `Engine::Free`, `FlushThreadCache`, `QueryMemoryStats`
- `Interface/HostServices.hxx` - split typed allocators (`AllocHeap`, `AllocFrame`, `CreatePool`, ...)

`Core/Public/EngineAllocator.hxx` (`FrameArena`, `ObjectPool`, ...) is **Core-internal**. It is not
marked `GE_API` and must not be included from plugin modules (`EngineMemory.hxx` pulls only the
Interface headers).

## 1. General-purpose heap = rpmalloc (single shared instance)

- `third_party/rpmalloc` is vendored and compiled (as C, with `ENABLE_OVERRIDE`,
  `ENABLE_PRELOAD`, `BUILD_DYNAMIC_LINK`) into **Core.dll only**, giving exactly
  one rpmalloc instance for the whole process.
- Core exports the `rp*` entry points via `Core/Rpmalloc.def`. Every other module
  routes its `operator new`/`delete` to that instance by including
  `Interface/RpmallocOverride.hxx` from one translation unit. Therefore memory
  allocated in one module can be freed in another and on any thread.
- `Engine::Allocate` / `Engine::Free` and `HostServices::AllocHeap` / `FreeHeap`
  both forward to rpmalloc. Both verify power-of-two alignment via `ENGINE_VM_VERIFY`.
- Windows caveat: on the dynamic CRT (/MD) the C `malloc`/`free` symbols are only
  fully replaced inside Core.dll. Engine code should allocate via `operator new`
  or `Engine::Allocate`. Raw `malloc` in third-party code stays on the CRT heap
  (and is freed by that same CRT), which is safe **as long as ownership never
  crosses the boundary** (engine allocates -> engine frees; third party allocates ->
  third party frees).

## 2. Temporary arenas = OS-direct bump (FrameArena, GPUArena)

- Reserve+commit one large block straight from the OS via the
  `PlatformVirtualAllocator` (VirtualAlloc / mmap) wrapper in
  `Core/Public/EngineVirtualMemory.hxx`.
- Allocation is a single pointer increment; alignment is computed against the
  absolute address. The whole arena is rewound at frame end via `Reset()`; there
  is no per-pointer free. `GPUArena` defaults to 256 B alignment for upload
  staging.
- Capacities are set at init time via `AllocatorConfig` (defaults: 16 MiB frame,
  64 MiB GPU).
- **Not thread-safe.** Current design: one global Frame/GPU arena on the main
  thread. **Future (job system):** one `FrameArena` per worker thread for
  lock-free scratch memory.

## 3. Object pool = fixed-size free list (ObjectPool + PoolHandle)

- Pre-sized free list for hot, same-sized objects. `Free` validates that the
  pointer lies on an object boundary inside the pool and uses an occupancy
  bitmap to reject double frees in all builds.
- Pools are created dynamically via `HostServices::CreatePool(objectSize, capacity)`
  and identified by an opaque `PoolHandle`. Size and capacity are fixed at
  creation time; `AllocPool` never takes a size argument.
- Not thread-safe; use one pool per owner thread or external serialisation.

## HostServices typed API (no ArenaId)

The old `Alloc(size, alignment, uint32_t arenaId)` entry point is removed.
Plugins call purpose-specific function pointers instead:

| Function | Purpose |
|----------|---------|
| `AllocHeap` / `FreeHeap` | Long-lived rpmalloc heap |
| `AllocFrame` | Per-frame scratch (reset via `ResetFrameArenas`) |
| `AllocGpu` | GPU upload staging (reset via `ResetFrameArenas`) |
| `CreatePool` / `DestroyPool` | Pool lifecycle |
| `AllocPool` / `FreePool` | Fixed-size pool alloc/free |

## General rules

Allocator APIs should define alignment, ownership, tracking, and failure behavior.
Use arenas/frame allocators for short-lived frame data and the rpmalloc heap for
long-lived engine objects. Leak detection and allocation tagging should be
available in debug builds (rpmalloc is built with `ENABLE_STATISTICS` in Debug).

## Known limitations / future work

- Per-thread FrameArena when Task Graph / job workers land.
- FrameArena decommit-on-reset for working-set control under memory pressure.

## Canonical user-facing doc

See `docs/memory.md` for API tables, usage examples, and test references.
