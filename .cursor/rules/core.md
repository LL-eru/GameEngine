# Core Rules

- Treat the engine as a long-lived product, not a demo. Prefer explicit contracts, stable ownership, and clear failure modes.
- Read nearby code before editing. Follow existing module boundaries unless the task is explicitly architectural.
- Keep public APIs minimal. Add parameters, globals, or virtual hooks only when there is a real caller and a documented reason.
- Do not hide important behavior in macros. Macros are allowed for export, platform, assert, and compile-time feature switches only.
- Every change must preserve buildability for Editor, Game, Core, Interface, Vulkan, DirectX12, and tools that depend on touched headers.
- When unsure, produce a small design note before implementation and state assumptions.

