# Editor Rules

- Editor workflows must not corrupt runtime assets on partial failure. Use temp files or transactional saves for important data.
- Editor-only dependencies stay out of runtime modules.
- UI actions that mutate engine state should be undoable or clearly scoped to temporary preview state.
- The editor may expose debug controls, but runtime code must define the actual invariant.
- Long asset operations should be cancelable or moved off the UI thread.
- Editor logs must include asset path, selected object, or operation name when available.

