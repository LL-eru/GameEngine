# Memory Docs

Purpose:
- Store durable design documents for the memory / allocator area.
- Keep accepted decisions, API contracts, migration notes, and validation evidence.

## Canonical references

| Document | Role |
|----------|------|
| [docs/memory.md](../../../docs/memory.md) | User-facing overview（現行 API・使用例・注意点） |
| [.cursor/memory/allocator_design.md](../../memory/allocator_design.md) | Agent / design memory（三層モデル・DLL 境界・制約） |

## Current API surface (plugins / hosts)

- `Interface/MemoryAPI.hxx` ? `Engine::Allocate`, `Engine::Free`, …
- `Interface/HostServices.hxx` ? `AllocHeap`, `AllocFrame`, `AllocGpu`, `CreatePool`, …

Core-internal types (`FrameArena`, `ObjectPool`, …) must not cross the DLL boundary.

## Suggested future documents

- `decisions.md` ? accepted ADRs (e.g. rpmalloc single instance, typed HostServices split)
- `roadmap.md` ? per-thread FrameArena, decommit-on-reset, allocation tagging
