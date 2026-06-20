# Error Handling Rules

- Separate programmer errors from runtime failures. Use asserts for impossible states and result/logging for recoverable failures.
- Every failed platform, filesystem, shader, renderer, or plugin call must include context in the log.
- Avoid silent fallback paths that make rendering or asset bugs hard to diagnose.
- Plugin and backend boundaries must not throw exceptions across module boundaries.
- Fatal engine initialization failures should fail early with a clear message and no partial runtime state.
- Recovery code must leave objects in a valid, documented state.

