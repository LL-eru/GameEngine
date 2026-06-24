# Refactor Workflow

1. Architect: define invariant to preserve and target dependency shape.
2. Refactor Agent: make small mechanical steps with buildable checkpoints.
3. Reviewer: look for behavior drift, API breakage, lifetime changes, and hidden coupling.
4. Tester: run regression checks for moved behavior.
5. Benchmark: compare hot paths when data layout or frame code changed.
6. Document: record new structure and migration notes.

