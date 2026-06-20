# Ownership Policy Memory

Creators define destruction, allocator, and thread affinity.

Use handles for resources that can reload, move, or be registry-owned. Use smart pointers for clear exclusive/shared ownership and raw pointers only for borrowed references.

GPU destruction must be frame-safe and account for in-flight command buffers.

