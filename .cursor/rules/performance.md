# Performance Rules

- Do not allocate per frame on hot paths unless the allocator and lifetime are explicit.
- Prefer contiguous storage, handles, spans, and cache-aware iteration for runtime systems.
- Measure before claiming speedups. Add a benchmark or profiling note for non-trivial optimizations.
- Keep debug diagnostics available, but make expensive validation controllable by build config or runtime flag.
- Avoid hidden synchronization, virtual calls, string formatting, and filesystem access in render, ECS, animation, and job hot loops.
- Favor predictable latency over peak throughput for frame-critical work.

