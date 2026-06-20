# Renderer Design Memory

The renderer should expose backend-neutral concepts: device, swapchain, command buffer, buffers, textures, shaders, pipelines, descriptors, fences, and frame context.

Backend implementations own API-specific handles and synchronization details.

Render passes should have labels, explicit resource use, and clear lifetime rules.

