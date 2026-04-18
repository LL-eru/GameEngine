#pragma once
#include "../Interface/IRenderer.hxx"

namespace Vulkan
{
    class VulkanCommandBuffer;

    class VulkanRenderer : public Render::IRenderer
    {
    public:
        bool Initialize(const Render::WindowHandle& window) override;
        void Shutdown() override;

        void BeginFrame() override;
        void EndFrame() override;

        Render::ICommandBuffer* BeginCommandBuffer() override;
        void Submit(Render::ICommandBuffer* cmd) override;

    private:
        void* m_Window; // GLFWwindow*

        // Vulkan objects
        VkInstance instance;
        VkDevice device;
        VkSwapchainKHR swapchain;

        VulkanCommandBuffer* m_CommandBuffer;
    };
}