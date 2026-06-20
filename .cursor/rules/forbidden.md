# Forbidden Rules

- Do not add global mutable singletons unless the architecture document names the lifetime and shutdown order.
- Do not introduce cyclic dependencies between Core, Interface, Editor, Game, and renderer backends.
- Do not swallow errors, ignore return values from platform/API calls, or leave TODOs as the only error handling.
- Do not put backend-specific API types in generic public interfaces without a wrapper or feature-gated escape hatch.
- Do not allocate or format strings in known per-frame hot paths without a documented reason.
- Do not perform destructive git operations or overwrite user changes unless explicitly requested.

