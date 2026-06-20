# Ownership Rules

- The creator of a resource defines its destruction path, allocator, and thread affinity.
- Public APIs must state whether pointers are owning, borrowed, optional, or retained.
- Prefer handles for engine resources that can move, reload, or be owned by registries.
- Do not expose references to internal containers if reload, compaction, or multithreaded access can invalidate them.
- GPU resources must have frame-safe retirement rules. Never destroy resources that may still be referenced by in-flight commands.
- Allocator ownership is part of object ownership; do not mix allocators without a clear boundary.

