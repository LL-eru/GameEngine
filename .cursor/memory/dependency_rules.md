# Dependency Rules Memory

Runtime code must not depend on editor UI libraries.

Generic interfaces must not include Vulkan, DirectX, Qt, ImGui, or platform headers unless the file is explicitly backend/tool specific.

When adding a dependency, document why the dependency direction is valid.

