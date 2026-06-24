# ECS Design Memory

Components are data. Systems own behavior and runtime caches.

Entity IDs should be stable enough for runtime references and should not be serialized as raw container positions.

System order, threading, and structural changes must be explicit.

