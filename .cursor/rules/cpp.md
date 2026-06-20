# C++ Rules

- Target modern C++ with RAII, move semantics, constexpr, strong types, and narrow includes.
- Prefer .hxx for public headers and .cxx for implementation to match this repository.
- Headers must be self-contained and include only what they use. Prefer forward declarations for private pointer/reference members.
- Avoid exceptions across engine/plugin boundaries. Use result types, error codes, asserts, or logged failures according to severity.
- Make ownership visible: unique_ptr, handles, spans, views, references, or raw non-owning pointers with documented lifetime.
- Keep templates out of public ABI unless they are header-only utilities with clear compile-time value.

