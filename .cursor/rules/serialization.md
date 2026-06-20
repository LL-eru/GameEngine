# Serialization Rules

- Serialized data is a compatibility contract. Include version fields for assets, scenes, prefabs, and caches.
- Separate runtime object identity from serialized stable IDs.
- Deserializers must validate required fields and produce actionable errors with asset path or object name.
- Do not serialize raw pointers, transient handles, platform handles, or frame-local state.
- Keep binary formats deterministic and endian/version aware when used for cache or shipping data.
- Prefer schema evolution over one-off migration code hidden in loaders.

