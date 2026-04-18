#pragma once
#include <cstdint>

namespace Render
{
    enum class GraphicsAPI
    {
        Vulkan,
        DirectX12
    };

    struct WindowHandle
    {
        void* handle; // GLFWwindow*
    };

    struct Extent2D
    {
        uint32_t width;
        uint32_t height;
    };
}