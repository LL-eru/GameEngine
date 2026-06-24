//	ファイル名	：VulkanCommandBuffer.cxx
//	  概  要		：Vulkan ICommandBuffer 実装
//	作	成	者	：daigo
//_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/

#include "VulkanCommandBuffer.hxx"
#include "VulkanRenderer.hxx"

void Vulkan::VulkanCommandBuffer::Setup(vk::CommandBuffer cmd, VulkanRenderer* renderer, uint32_t imageIndex)
{
    m_cmd        = cmd;
    m_renderer   = renderer;
    m_imageIndex = imageIndex;
}

void Vulkan::VulkanCommandBuffer::Begin()
{
    m_cmd.reset();

    vk::CommandBufferBeginInfo beginInfo;
    m_cmd.begin(beginInfo);
}

void Vulkan::VulkanCommandBuffer::Clear(float r, float g, float b, float a)
{
    // レンダーパスをクリアカラー付きで開始し、パイプライン/バッファをバインドする
    vk::ClearValue clearVal;
    clearVal.color.float32[0] = r;
    clearVal.color.float32[1] = g;
    clearVal.color.float32[2] = b;
    clearVal.color.float32[3] = a;

    vk::Extent2D extent = m_renderer->GetSwapchainExtent();

    vk::RenderPassBeginInfo rpBeginInfo;
    rpBeginInfo.renderPass      = m_renderer->GetRenderPass();
    rpBeginInfo.framebuffer     = m_renderer->GetFramebuffer(m_imageIndex);
    rpBeginInfo.renderArea      = vk::Rect2D({0, 0}, extent);
    rpBeginInfo.clearValueCount = 1;
    rpBeginInfo.pClearValues    = &clearVal;

    m_cmd.beginRenderPass(rpBeginInfo, vk::SubpassContents::eInline);

    m_cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_renderer->GetPipeline());

    vk::Buffer     vertexBuffers[] = { m_renderer->GetVertexBuffer(), m_renderer->GetVertexBuffer() };
    vk::DeviceSize offsets[]       = { 0, 0 };
    m_cmd.bindVertexBuffers(0, 2, vertexBuffers, offsets);

    m_cmd.bindIndexBuffer(m_renderer->GetIndexBuffer(), 0, vk::IndexType::eUint16);

    vk::DescriptorSet descSets[] = {
        m_renderer->GetDescriptorSet(0),
        m_renderer->GetDescriptorSet(1)
    };
    m_cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        m_renderer->GetPipelineLayout(),
        0, 2, descSets,
        0, nullptr);
}

void Vulkan::VulkanCommandBuffer::Draw(unsigned long vertexCount)
{
    uint32_t indexCount = (vertexCount > 0)
        ? static_cast<uint32_t>(vertexCount)
        : m_renderer->GetIndexCount();

    // 1つ目のクワッド (sceneData1)
    m_cmd.drawIndexed(indexCount, 1, 0, 0, 0);
    // 2つ目のクワッド (sceneData2)
    m_cmd.drawIndexed(indexCount, 1, 0, 0, 0);
}

void Vulkan::VulkanCommandBuffer::End()
{
    m_cmd.endRenderPass();
    m_cmd.end();
}
