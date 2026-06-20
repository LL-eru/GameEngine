# Rendering Rules

- Keep renderer interface concepts backend-neutral: device, swapchain, command buffer, buffer, texture, pipeline, shader, descriptor, fence.
- Backend files may use native API names; shared renderer contracts should not.
- Use explicit resource states, lifetimes, and synchronization. Hidden transitions are allowed only when documented.
- Shader inputs, descriptor layouts, vertex formats, and pipeline state must be versioned or validated together.
- Avoid blocking GPU waits during normal frame flow. Use frame fences and deferred destruction.
- RenderDoc/PIX-friendly labels are required for new render passes and important GPU resources when supported.

