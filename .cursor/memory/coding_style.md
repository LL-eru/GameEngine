# Coding Style Memory

Use .hxx for public headers and .cxx for implementation in engine modules.

Prefer RAII, explicit ownership, narrow includes, and self-contained headers. Avoid broad refactors during feature work.

Keep comments rare and useful: document invariants, lifetime rules, thread affinity, and non-obvious API requirements.

