# Threading Rules

- Thread ownership must be explicit: main thread, render thread, worker thread, IO thread, or job worker.
- Shared mutable state requires a named synchronization strategy. Do not add ad-hoc mutexes to hide design problems.
- Jobs must not capture references whose lifetime is shorter than the job. Prefer handles or copied immutable data.
- Renderer submission and resource lifetime must define which thread records, owns, and destroys GPU objects.
- Avoid blocking waits on the main thread during frame execution. Use fences, staging queues, or frame-delayed retirement.
- Document memory ordering when atomics are not simple counters or flags.

