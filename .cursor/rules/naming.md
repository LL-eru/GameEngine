# Naming Rules

- Names must reveal domain intent before implementation detail.
- Types use PascalCase. Functions and variables follow the local file style; do not mix styles inside one module.
- Boolean names read as predicates: IsReady, HasFailed, ShouldResize.
- Resource handles should include the resource kind: TextureHandle, BufferHandle, ShaderHandle.
- Backend-specific types carry backend names: VulkanDevice, Dx12CommandQueue.
- Avoid vague names such as Manager, Data, Info, and Util unless the surrounding API already makes the role precise.

