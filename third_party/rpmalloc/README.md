# rpmalloc (vendored)

Vendored copy of [mjansson/rpmalloc](https://github.com/mjansson/rpmalloc).

- Version / tag: **1.4.5**
- Files: `rpmalloc.h`, `rpmalloc.c`, `rpnew.h`, `malloc.c`, `LICENSE`
- License: Public Domain (see `LICENSE`)

## Why this dependency

The engine's general-purpose heap is delegated to rpmalloc, a lock-free
thread-caching allocator. A single rpmalloc instance is compiled into
`Core.dll` and exported; every other module routes `operator new/delete`
(and `Engine::Allocate/Free`) to that one instance so memory allocated in
one module can be freed in another (cross-module / cross-thread free).

Dependency direction: `Core` -> `third_party/rpmalloc` only. No other module
compiles `rpmalloc.c`; they import the exported `rp*` symbols from `Core.dll`.

## Build integration

- `Core.dll` compiles `rpmalloc.c` (as C) with `ENABLE_OVERRIDE=1`,
  `ENABLE_PRELOAD=1`, `BUILD_DYNAMIC_LINK=1`. This provides the C
  `malloc/free/...` overrides and an auto-init `DllMain` for the process and
  per-thread caches. The `rp*` entry points are exported via `Core/Rpmalloc.def`.
- Other modules include `Interface/RpmallocOverride.hxx` from one translation
  unit to override `operator new/delete` (forwarding to the imported `rp*`).
