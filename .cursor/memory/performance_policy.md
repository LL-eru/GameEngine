# Performance Policy Memory

Per-frame hot paths should avoid heap allocation, blocking IO, hidden locks, and string formatting.

Performance work requires a named metric and a repeatable workload.

Prefer predictable frame time over peak throughput when trade-offs conflict.

