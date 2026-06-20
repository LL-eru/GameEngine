# Testing Rules

- Add focused tests for parsing, serialization, resource lifetime, allocators, math, ECS behavior, and regressions.
- Rendering changes need at least build validation and, when possible, a small deterministic frame or shader validation path.
- Bugs require a regression test or a documented reason why the behavior cannot be tested yet.
- Avoid tests that depend on local absolute paths, timing, GPU vendor behavior, or editor window focus.
- Benchmarks must be separate from correctness tests and must state workload size and expected signal.
- Test names should describe behavior, not implementation.

