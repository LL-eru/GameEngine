# Rendering Workflow

1. Renderer: define backend-neutral goal and frame/resource lifecycle.
2. Vulkan/DX12: map contracts to backend-specific resources and synchronization.
3. Shader: validate shader inputs, reflection, descriptors, and variants.
4. Reviewer: inspect barriers, lifetime, labels, error handling, and backend leakage.
5. Tester: build backend targets and run validation-layer/debug-layer checks.
6. Benchmark: capture frame time, GPU waits, allocations, and pass cost.
7. Document: update renderer design and usage examples.

