#pragma once
#include "../Interface/ICommandBuffer.hxx"

namespace Vulkan
{
    class VulkanCommandBuffer : public Render::ICommandBuffer
    {
    public:
        void Begin() override;
        void End() override;

        void Clear(float r, float g, float b, float a) override;
        void Draw(unsigned long vertexCount) override;

    private:
        VkCommandBuffer cmd;
    };
}