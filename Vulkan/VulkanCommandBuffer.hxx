#pragma once

#include "../Interface/ICommandBuffer.hxx"

#include <vulkan/vulkan.hpp>

namespace Vulkan
{
    class VulkanRenderer;

    class VulkanCommandBuffer : public Render::ICommandBuffer
    {
    public:
        void Setup(vk::CommandBuffer cmd, VulkanRenderer* renderer, uint32_t imageIndex);

        void Begin() override;
        void End() override;

        void Clear(float r, float g, float b, float a) override;
        void Draw(unsigned long vertexCount) override;

    private:
        vk::CommandBuffer m_cmd;
        VulkanRenderer* m_renderer = nullptr;
        uint32_t m_imageIndex = 0;
    };
}
