#pragma once

#include "../Interface/IRenderer.hxx"

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan.h>

#include <vector>
#include <string>

struct GLFWwindow;

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

        uint32_t GetIndexCount() const;
        vk::DescriptorSet GetDescriptorSet(uint32_t i) const;
        vk::Extent2D GetSwapchainExtent() const { return m_swapchainExtent; }
        vk::RenderPass GetRenderPass() const { return m_Renderpass.get(); }
        vk::Framebuffer GetFramebuffer(uint32_t index) const { return m_swapchainFramebufs[index].get(); }
        vk::Pipeline GetPipeline() const { return m_Pipeline.get(); }
        vk::Buffer GetVertexBuffer() const { return vertexBuf.get(); }
        vk::Buffer GetIndexBuffer() const { return indexBuf.get(); }
        vk::PipelineLayout GetPipelineLayout() const { return pipelineLayout.get(); }

    private:
        void RecreateSwapchain();

        GLFWwindow* m_Window = nullptr;

        vk::UniqueInstance m_Instance;
        vk::UniqueSurfaceKHR m_uSurface;
        vk::PhysicalDevice physicalDevice;
        vk::UniqueDevice m_Device;
        vk::Queue m_graphicsQueue;
        uint32_t m_graphicsQueueFamilyIndex = 0;

        std::vector<const char*> m_extensions;
        std::vector<const char*> m_Layers;

#if defined(_DEBUG)
        VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
        PFN_vkDestroyDebugUtilsMessengerEXT m_pfnDestroyDebugMessenger = nullptr;
#endif

        vk::UniqueBuffer vertexBuf;
        vk::UniqueDeviceMemory vertexBufMemory;
        vk::UniqueBuffer indexBuf;
        vk::UniqueDeviceMemory indexBufMemory;
        vk::UniqueBuffer uniformBuf;
        vk::UniqueDeviceMemory uniformBufMemory;
        void* pUniformBufMem = nullptr;
        vk::UniqueBuffer uniformBuf2;
        vk::UniqueDeviceMemory uniformBufMemory2;
        void* pUniformBufMem2 = nullptr;

        vk::VertexInputBindingDescription vertexBindingDescription[1]{};
        vk::VertexInputAttributeDescription vertexInputDescription[2]{};

        vk::UniqueDescriptorSetLayout descSetLayout;
        vk::UniqueDescriptorPool descPool;
        std::vector<vk::UniqueDescriptorSet> descSets;
        vk::UniqueDescriptorSetLayout descSetLayout2;
        vk::UniqueDescriptorPool descPool2;
        std::vector<vk::UniqueDescriptorSet> descSets2;

        vk::SurfaceFormatKHR swapchainFormat{};
        vk::PresentModeKHR swapchainPresentMode{};
        vk::UniqueSwapchainKHR m_Swapchain;
        std::vector<vk::Image> swapchainImages;
        vk::Extent2D m_swapchainExtent{};
        std::vector<vk::UniqueImageView> m_swapchainImageViews;
        std::vector<vk::UniqueFramebuffer> m_swapchainFramebufs;

        vk::UniqueRenderPass m_Renderpass;
        vk::UniquePipelineLayout pipelineLayout;
        vk::UniqueShaderModule vertShader;
        vk::UniqueShaderModule fragShader;
        vk::UniquePipeline m_Pipeline;

        vk::UniqueCommandPool cmdPool;
        std::vector<vk::UniqueCommandBuffer> m_CmdBufs;

        vk::UniqueSemaphore m_swapchainImgSemaphore;
        vk::UniqueSemaphore imgRenderedSemaphore;
        vk::UniqueFence imgRenderedFence;

        VulkanCommandBuffer* m_pCmdBufWrapper = nullptr;
        uint32_t m_currentImageIndex = 0;
        bool m_frameBegan = false;
    };
}
