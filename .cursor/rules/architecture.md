# Architecture Rules

- Core owns platform-agnostic primitives: logging, allocation, assertions, plugin ABI, and common types.
- Interface defines stable contracts between host, game, editor, and renderer. It must not depend on backend implementation details.
- Renderer backends implement contracts; they do not leak Vulkan, DX12, or platform handles through generic interfaces unless explicitly wrapped.
- Editor code may depend on engine systems, but engine runtime code must not depend on editor-only UI or tooling.
- Prefer dependency direction: Game -> Interface -> Core, Editor -> Interface/Core, Backend -> Interface/Core.
- Architectural changes require a short note in .cursor/memory or .cursor/docs describing the new invariant.

