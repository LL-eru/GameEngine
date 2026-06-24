# Engine Architecture Memory

The intended dependency direction is Game/Editor -> Interface -> Core, with renderer backends depending on Interface/Core rather than the reverse.

Core should stay platform-lean and provide shared primitives: logging, assertions, allocation, plugin exports, and common engine definitions.

Interface is the contract layer for host services, renderer APIs, and plugin-facing types. Avoid backend-specific types here unless they are intentionally wrapped.

