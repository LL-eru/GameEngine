# Allocator Design Memory

Allocator APIs should define alignment, ownership, tracking, and failure behavior.

Use arenas or frame allocators for short-lived frame data. Use persistent allocators for long-lived engine objects.

Leak detection and allocation tagging should be available in debug builds.

