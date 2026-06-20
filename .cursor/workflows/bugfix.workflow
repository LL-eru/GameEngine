# Bugfix Workflow

1. Planner: reproduce or describe failure, suspected modules, and expected behavior.
2. Debugger: gather logs, assertions, validation errors, call paths, or minimal repro.
3. Specialist: fix root cause in owning module.
4. Reviewer: confirm no broad fallback or masking behavior was added.
5. Regression Test: add a focused test or smoke check.
6. Document: update memory if the bug reveals a new invariant.

