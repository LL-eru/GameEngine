//	ファイル名	：VulkanRenderer.cxx
//	  概  要		：Vulkan レンダラー実装 (VulkanApp.cxx を GLFW ベースに移植)
//	作	成	者	：daigo
//_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/

#include "VulkanRenderer.hxx"
#include "VulkanCommandBuffer.hxx"
#include "../Interface/RendererAPI.hxx"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <fstream>
#include <sstream>
#include <chrono>
#include <cmath>

// =-=-= 頂点データ =-=-=

struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };

struct Vertex {
    Vec2 Pos;
    Vec3 Color;
};

static std::vector<Vertex> s_vertices = {
    Vertex{Vec2{-0.1f,  0.1f}, Vec3{0.0f, 1.0f, 1.0f}},
    Vertex{Vec2{-0.1f, -0.1f}, Vec3{1.0f, 1.0f, 0.0f}},
    Vertex{Vec2{ 0.1f, -0.1f}, Vec3{1.0f, 0.0f, 1.0f}},
    Vertex{Vec2{ 0.1f,  0.1f}, Vec3{1.0f, 1.0f, 1.0f}},
};
static std::vector<uint16_t> s_indices = { 0, 1, 2, 0, 2, 3 };

struct SceneData {
    Vec2 rectCenter;
};

static SceneData s_sceneData1 = { Vec2{0.3f, -0.2f} };
static SceneData s_sceneData2 = { Vec2{0.0f,  0.0f} };

// =-=-= Validation レイヤーコールバック =-=-=

#if defined(_DEBUG)
static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* pData,
    void*                                       /*pUserData*/)
{
    if (!pData) return VK_FALSE;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        LOG_ERROR("Vulkan/Validation", std::string(pData->pMessage));
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOG_WARN("Vulkan/Validation", std::string(pData->pMessage));
    } else {
        LOG_INFO("Vulkan/Validation", std::string(pData->pMessage));
    }
    return VK_FALSE;
}
#endif

// =-=-= ユーティリティ =-=-=

static uint32_t FindMemoryType(
    const vk::PhysicalDeviceMemoryProperties& memProps,
    uint32_t memTypeBits,
    vk::MemoryPropertyFlags requiredFlags)
{
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & requiredFlags) == requiredFlags)
            return i;
    }
    return UINT32_MAX;
}

static void UploadBufferWithStaging(
    vk::Device device,
    vk::Queue queue,
    uint32_t queueFamilyIndex,
    const vk::PhysicalDeviceMemoryProperties& memProps,
    vk::Buffer dst,
    const void* data,
    vk::DeviceSize size)
{
    vk::BufferCreateInfo stagingInfo;
    stagingInfo.size        = size;
    stagingInfo.usage       = vk::BufferUsageFlagBits::eTransferSrc;
    stagingInfo.sharingMode = vk::SharingMode::eExclusive;

    auto stagingBuf = device.createBufferUnique(stagingInfo);
    auto memReq     = device.getBufferMemoryRequirements(stagingBuf.get());

    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memProps, memReq.memoryTypeBits,
                                    vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent);

    auto stagingMem = device.allocateMemoryUnique(allocInfo);
    device.bindBufferMemory(stagingBuf.get(), stagingMem.get(), 0);

    void* mapped = device.mapMemory(stagingMem.get(), 0, size);
    std::memcpy(mapped, data, size);
    device.unmapMemory(stagingMem.get());

    vk::CommandPoolCreateInfo poolInfo;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags            = vk::CommandPoolCreateFlagBits::eTransient;
    auto tmpPool = device.createCommandPoolUnique(poolInfo);

    vk::CommandBufferAllocateInfo cmdAllocInfo;
    cmdAllocInfo.commandPool        = tmpPool.get();
    cmdAllocInfo.commandBufferCount = 1;
    cmdAllocInfo.level              = vk::CommandBufferLevel::ePrimary;
    auto tmpCmdBufs = device.allocateCommandBuffersUnique(cmdAllocInfo);

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    tmpCmdBufs[0]->begin(beginInfo);

    vk::BufferCopy region;
    region.size = size;
    tmpCmdBufs[0]->copyBuffer(stagingBuf.get(), dst, {region});
    tmpCmdBufs[0]->end();

    vk::CommandBuffer submitBuf = tmpCmdBufs[0].get();
    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &submitBuf;
    queue.submit({submitInfo});
    queue.waitIdle();
}

// =-=-= アクセサ実裁E=-=-=

uint32_t Vulkan::VulkanRenderer::GetIndexCount() const
{
    return static_cast<uint32_t>(s_indices.size());
}

vk::DescriptorSet Vulkan::VulkanRenderer::GetDescriptorSet(uint32_t i) const
{
    if (i == 0) return descSets[0].get();
    return descSets2[0].get();
}

// =-=-= Initialize =-=-=

bool Vulkan::VulkanRenderer::Initialize(const Render::WindowHandle& window)
{
    m_Window = static_cast<GLFWwindow*>(window.handle);

    // GLFW Vulkan 拡張の取得
    uint32_t extCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
    if (!glfwExts) { LOG_ERROR("Vulkan", "GLFW Vulkan 拡張を取得できませんでした。");
        return false;
    }
    m_extensions.assign(glfwExts, glfwExts + extCount);

#if defined(_DEBUG)
    m_Layers.push_back("VK_LAYER_KHRONOS_validation");
    m_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    // インスタンスの作成
    vk::ApplicationInfo appInfo;
    appInfo.setPApplicationName("GameEngine")
           .setApplicationVersion(1)
           .setPEngineName("GameEngine")
           .setEngineVersion(1)
           .setApiVersion(VK_API_VERSION_1_3);

    vk::InstanceCreateInfo instInfo;
    instInfo.setFlags(vk::InstanceCreateFlags())
            .setPApplicationInfo(&appInfo)
            .setEnabledExtensionCount(static_cast<uint32_t>(m_extensions.size()))
            .setPpEnabledExtensionNames(m_extensions.data())
            .setEnabledLayerCount(static_cast<uint32_t>(m_Layers.size()))
            .setPpEnabledLayerNames(m_Layers.data());

    try {
        m_Instance = vk::createInstanceUnique(instInfo);
    }
    catch (const std::exception& e) {
        LOG_ERROR("Vulkan", std::string("インスタンス作成失敗: ") + e.what());
        return false;
    }

#if defined(_DEBUG)
    {
        auto pfnCreate = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(static_cast<VkInstance>(m_Instance.get()),
                                  "vkCreateDebugUtilsMessengerEXT"));
        m_pfnDestroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(static_cast<VkInstance>(m_Instance.get()),
                                  "vkDestroyDebugUtilsMessengerEXT"));
        if (pfnCreate) {
            VkDebugUtilsMessengerCreateInfoEXT dbgInfo{};
            dbgInfo.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            dbgInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            dbgInfo.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            dbgInfo.pfnUserCallback = VulkanDebugCallback;
            pfnCreate(static_cast<VkInstance>(m_Instance.get()), &dbgInfo, nullptr, &m_debugMessenger);
        } else {
            LOG_WARN("Vulkan", "vkCreateDebugUtilsMessengerEXT が見つかりません。Validation メッセージは無効。");
        }
    }
#endif

    // サーフェスの作成 (GLFW)
    VkSurfaceKHR rawSurface;
    if (glfwCreateWindowSurface(static_cast<VkInstance>(m_Instance.get()),
                                m_Window, nullptr, &rawSurface) != VK_SUCCESS) { LOG_ERROR("Vulkan", "サーフェス作成失敗。"); return false; } m_uSurface = vk::UniqueSurfaceKHR(rawSurface, m_Instance.get());

    // 物理デバイスの選定
    auto physicalDevices = m_Instance->enumeratePhysicalDevices();
    bool foundDevice = false;

    for (auto& pd : physicalDevices) {
        auto queueProps = pd.getQueueFamilyProperties();
        bool hasGraphics = false;

        for (uint32_t j = 0; j < queueProps.size(); j++) {
            if ((queueProps[j].queueFlags & vk::QueueFlagBits::eGraphics) &&
                pd.getSurfaceSupportKHR(j, m_uSurface.get())) {
                m_graphicsQueueFamilyIndex = j;
                hasGraphics = true;
                break;
            }
        }

        bool hasSwapchain = false;
        for (auto& ext : pd.enumerateDeviceExtensionProperties()) {
            if (std::string(ext.extensionName.data()) == VK_KHR_SWAPCHAIN_EXTENSION_NAME) {
                hasSwapchain = true;
                break;
            }
        }

        if (hasGraphics && hasSwapchain) {
            physicalDevice = pd;
            foundDevice    = true;
            break;
        }
    }

    if (!foundDevice) { LOG_ERROR("Vulkan", "適切な物理デバイスが見つかりません。");
        return false;
    }

    // 論理デバイスの作成
    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    float priority = 1.0f;

    vk::DeviceQueueCreateInfo queueCreateInfo;
    queueCreateInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;
    queueCreateInfo.queueCount       = 1;
    queueCreateInfo.pQueuePriorities = &priority;

    vk::DeviceCreateInfo devCreateInfo;
    devCreateInfo.pQueueCreateInfos       = &queueCreateInfo;
    devCreateInfo.queueCreateInfoCount    = 1;
    devCreateInfo.enabledExtensionCount   = 1;
    devCreateInfo.ppEnabledExtensionNames = devExts;

    m_Device = physicalDevice.createDeviceUnique(devCreateInfo);
    m_graphicsQueue = m_Device->getQueue(m_graphicsQueueFamilyIndex, 0);

    auto memProps = physicalDevice.getMemoryProperties();

    // =-=-= 頂点バッファの作成 =-=-=
    {
        vk::BufferCreateInfo bufInfo;
        bufInfo.size        = sizeof(Vertex) * s_vertices.size();
        bufInfo.usage       = vk::BufferUsageFlagBits::eVertexBuffer |
                              vk::BufferUsageFlagBits::eTransferDst;
        bufInfo.sharingMode = vk::SharingMode::eExclusive;

        vertexBuf = m_Device->createBufferUnique(bufInfo);
        auto memReq = m_Device->getBufferMemoryRequirements(vertexBuf.get());

        vk::MemoryAllocateInfo allocInfo;
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memProps, memReq.memoryTypeBits,
                                        vk::MemoryPropertyFlagBits::eDeviceLocal);
        if (allocInfo.memoryTypeIndex == UINT32_MAX) {
            // フォールバック: HostVisible
            allocInfo.memoryTypeIndex = FindMemoryType(memProps, memReq.memoryTypeBits,
                                            vk::MemoryPropertyFlagBits::eHostVisible |
                                            vk::MemoryPropertyFlagBits::eHostCoherent);
        }
        if (allocInfo.memoryTypeIndex == UINT32_MAX) {
            LOG_ERROR("Vulkan", "イメージ取得失敗。");
            return false;
        }

        vertexBufMemory = m_Device->allocateMemoryUnique(allocInfo);
        m_Device->bindBufferMemory(vertexBuf.get(), vertexBufMemory.get(), 0);

        UploadBufferWithStaging(m_Device.get(), m_graphicsQueue,
                                m_graphicsQueueFamilyIndex, memProps,
                                vertexBuf.get(),
                                s_vertices.data(),
                                sizeof(Vertex) * s_vertices.size());
    }

    vertexBindingDescription[0].binding   = 0;
    vertexBindingDescription[0].stride    = sizeof(Vertex);
    vertexBindingDescription[0].inputRate = vk::VertexInputRate::eVertex;

    vertexInputDescription[0].binding  = 0;
    vertexInputDescription[0].location = 0;
    vertexInputDescription[0].format   = vk::Format::eR32G32Sfloat;
    vertexInputDescription[0].offset   = offsetof(Vertex, Pos);
    vertexInputDescription[1].binding  = 0;
    vertexInputDescription[1].location = 1;
    vertexInputDescription[1].format   = vk::Format::eR32G32B32Sfloat;
    vertexInputDescription[1].offset   = offsetof(Vertex, Color);

    // =-=-= インデックスバッファの作成 =-=-=
    {
        vk::BufferCreateInfo bufInfo;
        bufInfo.size        = sizeof(uint16_t) * s_indices.size();
        bufInfo.usage       = vk::BufferUsageFlagBits::eIndexBuffer |
                              vk::BufferUsageFlagBits::eTransferDst;
        bufInfo.sharingMode = vk::SharingMode::eExclusive;

        indexBuf = m_Device->createBufferUnique(bufInfo);
        auto memReq = m_Device->getBufferMemoryRequirements(indexBuf.get());

        vk::MemoryAllocateInfo allocInfo;
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memProps, memReq.memoryTypeBits,
                                        vk::MemoryPropertyFlagBits::eHostVisible |
                                        vk::MemoryPropertyFlagBits::eHostCoherent);
        if (allocInfo.memoryTypeIndex == UINT32_MAX) {
            LOG_ERROR("Vulkan", "イメージ取得失敗。");
            return false;
        }

        indexBufMemory = m_Device->allocateMemoryUnique(allocInfo);
        m_Device->bindBufferMemory(indexBuf.get(), indexBufMemory.get(), 0);

        UploadBufferWithStaging(m_Device.get(), m_graphicsQueue,
                                m_graphicsQueueFamilyIndex, memProps,
                                indexBuf.get(),
                                s_indices.data(),
                                sizeof(uint16_t) * s_indices.size());
    }

    // =-=-= ユニフォームバッファ 1 の作成 =-=-=
    {
        vk::BufferCreateInfo bufInfo;
        bufInfo.size        = sizeof(SceneData);
        bufInfo.usage       = vk::BufferUsageFlagBits::eUniformBuffer;
        bufInfo.sharingMode = vk::SharingMode::eExclusive;

        uniformBuf = m_Device->createBufferUnique(bufInfo);
        auto memReq = m_Device->getBufferMemoryRequirements(uniformBuf.get());

        vk::MemoryAllocateInfo allocInfo;
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memProps, memReq.memoryTypeBits,
                                        vk::MemoryPropertyFlagBits::eHostVisible |
                                        vk::MemoryPropertyFlagBits::eHostCoherent);
        if (allocInfo.memoryTypeIndex == UINT32_MAX) {
            LOG_ERROR("Vulkan", "イメージ取得失敗。");
            return false;
        }

        uniformBufMemory = m_Device->allocateMemoryUnique(allocInfo);
        m_Device->bindBufferMemory(uniformBuf.get(), uniformBufMemory.get(), 0);
        pUniformBufMem = m_Device->mapMemory(uniformBufMemory.get(), 0, sizeof(SceneData));
    }

    // =-=-= ユニフォームバッファ 2 の作成 =-=-=
    {
        vk::BufferCreateInfo bufInfo;
        bufInfo.size        = sizeof(SceneData);
        bufInfo.usage       = vk::BufferUsageFlagBits::eUniformBuffer;
        bufInfo.sharingMode = vk::SharingMode::eExclusive;

        uniformBuf2 = m_Device->createBufferUnique(bufInfo);
        auto memReq = m_Device->getBufferMemoryRequirements(uniformBuf2.get());

        vk::MemoryAllocateInfo allocInfo;
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memProps, memReq.memoryTypeBits,
                                        vk::MemoryPropertyFlagBits::eHostVisible |
                                        vk::MemoryPropertyFlagBits::eHostCoherent);
        if (allocInfo.memoryTypeIndex == UINT32_MAX) {
            LOG_ERROR("Vulkan", "イメージ取得失敗。");
            return false;
        }

        uniformBufMemory2 = m_Device->allocateMemoryUnique(allocInfo);
        m_Device->bindBufferMemory(uniformBuf2.get(), uniformBufMemory2.get(), 0);
        pUniformBufMem2 = m_Device->mapMemory(uniformBufMemory2.get(), 0, sizeof(SceneData));
    }

    // =-=-= デスクリプタセットレイアウト / プール / セット (set 0) =-=-=
    {
        vk::DescriptorSetLayoutBinding binding;
        binding.binding         = 0;
        binding.descriptorType  = vk::DescriptorType::eUniformBuffer;
        binding.descriptorCount = 1;
        binding.stageFlags      = vk::ShaderStageFlagBits::eVertex;

        vk::DescriptorSetLayoutCreateInfo layoutInfo;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings    = &binding;
        descSetLayout = m_Device->createDescriptorSetLayoutUnique(layoutInfo);

        vk::DescriptorPoolSize poolSize;
        poolSize.type            = vk::DescriptorType::eUniformBuffer;
        poolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes    = &poolSize;
        poolInfo.maxSets       = 1;
        descPool = m_Device->createDescriptorPoolUnique(poolInfo);

        vk::DescriptorSetAllocateInfo allocInfo;
        auto layout = descSetLayout.get();
        allocInfo.descriptorPool     = descPool.get();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts        = &layout;
        descSets = m_Device->allocateDescriptorSetsUnique(allocInfo);

        vk::DescriptorBufferInfo bufInfo;
        bufInfo.buffer = uniformBuf.get();
        bufInfo.offset = 0;
        bufInfo.range  = sizeof(SceneData);

        vk::WriteDescriptorSet write;
        write.dstSet          = descSets[0].get();
        write.dstBinding      = 0;
        write.descriptorType  = vk::DescriptorType::eUniformBuffer;
        write.descriptorCount = 1;
        write.pBufferInfo     = &bufInfo;
        m_Device->updateDescriptorSets({write}, {});
    }

    // =-=-= デスクリプタセットレイアウト / プール / セット (set 1) =-=-=
    {
        vk::DescriptorSetLayoutBinding binding;
        binding.binding         = 0;
        binding.descriptorType  = vk::DescriptorType::eUniformBuffer;
        binding.descriptorCount = 1;
        binding.stageFlags      = vk::ShaderStageFlagBits::eVertex;

        vk::DescriptorSetLayoutCreateInfo layoutInfo;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings    = &binding;
        descSetLayout2 = m_Device->createDescriptorSetLayoutUnique(layoutInfo);

        vk::DescriptorPoolSize poolSize;
        poolSize.type            = vk::DescriptorType::eUniformBuffer;
        poolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes    = &poolSize;
        poolInfo.maxSets       = 1;
        descPool2 = m_Device->createDescriptorPoolUnique(poolInfo);

        vk::DescriptorSetAllocateInfo allocInfo;
        auto layout2 = descSetLayout2.get();
        allocInfo.descriptorPool     = descPool2.get();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts        = &layout2;
        descSets2 = m_Device->allocateDescriptorSetsUnique(allocInfo);

        vk::DescriptorBufferInfo bufInfo;
        bufInfo.buffer = uniformBuf2.get();
        bufInfo.offset = 0;
        bufInfo.range  = sizeof(SceneData);

        vk::WriteDescriptorSet write;
        write.dstSet          = descSets2[0].get();
        write.dstBinding      = 0;
        write.descriptorType  = vk::DescriptorType::eUniformBuffer;
        write.descriptorCount = 1;
        write.pBufferInfo     = &bufInfo;
        m_Device->updateDescriptorSets({write}, {});
    }

    // スワップチェーン / レンダーパス / パイプラインの作成
    auto surfaceFormats      = physicalDevice.getSurfaceFormatsKHR(m_uSurface.get());
    auto surfacePresentModes = physicalDevice.getSurfacePresentModesKHR(m_uSurface.get());
    swapchainFormat      = surfaceFormats[0];
    swapchainPresentMode = surfacePresentModes[0];

    RecreateSwapchain();

    // =-=-= コマンドプール / コマンドバッファ =-=-=
    {
        vk::CommandPoolCreateInfo poolInfo;
        poolInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;
        poolInfo.flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        cmdPool = m_Device->createCommandPoolUnique(poolInfo);

        vk::CommandBufferAllocateInfo allocInfo;
        allocInfo.commandPool        = cmdPool.get();
        allocInfo.commandBufferCount = 1;
        allocInfo.level              = vk::CommandBufferLevel::ePrimary;
        m_CmdBufs = m_Device->allocateCommandBuffersUnique(allocInfo);
    }

    // 同期オブジェクトの作成
    {
        vk::SemaphoreCreateInfo semInfo;
        m_swapchainImgSemaphore = m_Device->createSemaphoreUnique(semInfo);
        imgRenderedSemaphore    = m_Device->createSemaphoreUnique(semInfo);

        vk::FenceCreateInfo fenceInfo;
        fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;
        imgRenderedFence = m_Device->createFenceUnique(fenceInfo);
    }

    m_pCmdBufWrapper = new VulkanCommandBuffer(); LOG_INFO("Vulkan", "初期化完了。"); return true;
}

// =-=-= RecreateSwapchain =-=-=

void Vulkan::VulkanRenderer::RecreateSwapchain()
{
    m_swapchainFramebufs.clear();
    m_swapchainImageViews.clear();
    m_Renderpass.reset();
    m_Pipeline.reset();
    m_Swapchain.reset();

    int w = 0, h = 0;
    glfwGetFramebufferSize(m_Window, &w, &h);
    uint32_t screenWidth  = static_cast<uint32_t>(w);
    uint32_t screenHeight = static_cast<uint32_t>(h);

    auto surfaceCaps = physicalDevice.getSurfaceCapabilitiesKHR(m_uSurface.get());

    // スワップチェーン
    vk::SwapchainCreateInfoKHR scInfo;
    scInfo.surface          = m_uSurface.get();
    scInfo.minImageCount    = surfaceCaps.minImageCount + 1;
    scInfo.imageFormat      = swapchainFormat.format;
    scInfo.imageColorSpace  = swapchainFormat.colorSpace;
    scInfo.imageExtent      = surfaceCaps.currentExtent;
    scInfo.imageArrayLayers = 1;
    scInfo.imageUsage       = vk::ImageUsageFlagBits::eColorAttachment;
    scInfo.imageSharingMode = vk::SharingMode::eExclusive;
    scInfo.preTransform     = surfaceCaps.currentTransform;
    scInfo.presentMode      = swapchainPresentMode;
    scInfo.clipped          = VK_TRUE;

    m_Swapchain     = m_Device->createSwapchainKHRUnique(scInfo);
    swapchainImages = m_Device->getSwapchainImagesKHR(m_Swapchain.get());
    m_swapchainExtent = surfaceCaps.currentExtent;

    // レンダーパス
    vk::AttachmentDescription attachment;
    attachment.format         = swapchainFormat.format;
    attachment.samples        = vk::SampleCountFlagBits::e1;
    attachment.loadOp         = vk::AttachmentLoadOp::eClear;
    attachment.storeOp        = vk::AttachmentStoreOp::eStore;
    attachment.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
    attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    attachment.initialLayout  = vk::ImageLayout::eUndefined;
    attachment.finalLayout    = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentReference colorRef;
    colorRef.attachment = 0;
    colorRef.layout     = vk::ImageLayout::eColorAttachmentOptimal;

    vk::SubpassDescription subpass;
    subpass.pipelineBindPoint    = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    vk::RenderPassCreateInfo rpInfo;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments    = &attachment;
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    m_Renderpass = m_Device->createRenderPassUnique(rpInfo);

    // パイプライン
    vk::Viewport viewport;
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(screenWidth);
    viewport.height   = static_cast<float>(screenHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vk::Rect2D scissor;
    scissor.offset = vk::Offset2D{0, 0};
    scissor.extent = vk::Extent2D{screenWidth, screenHeight};

    vk::PipelineViewportStateCreateInfo viewportState;
    viewportState.viewportCount = 1;
    viewportState.pViewports    = &viewport;
    viewportState.scissorCount  = 1;
    viewportState.pScissors     = &scissor;

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
    vertexInputInfo.vertexBindingDescriptionCount   = 1;
    vertexInputInfo.pVertexBindingDescriptions      = vertexBindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions    = vertexInputDescription;

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
    inputAssembly.topology               = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    vk::PipelineRasterizationStateCreateInfo rasterizer;
    rasterizer.depthClampEnable        = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode             = vk::PolygonMode::eFill;
    rasterizer.lineWidth               = 1.0f;
    rasterizer.cullMode                = vk::CullModeFlagBits::eBack;
    rasterizer.frontFace               = vk::FrontFace::eClockwise;
    rasterizer.depthBiasEnable         = VK_FALSE;

    vk::PipelineMultisampleStateCreateInfo multisample;
    multisample.sampleShadingEnable  = VK_FALSE;
    multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendAttachmentState blendAttachment;
    blendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    blendAttachment.blendEnable = VK_FALSE;

    vk::PipelineColorBlendStateCreateInfo blend;
    blend.logicOpEnable = VK_FALSE;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blendAttachment;

    vk::DescriptorSetLayout layouts[] = { descSetLayout.get(), descSetLayout2.get() };
    vk::PipelineLayoutCreateInfo layoutInfo;
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts    = layouts;
    pipelineLayout = m_Device->createPipelineLayoutUnique(layoutInfo);

    // シェーダーの読み込み
    auto loadSpv = [](const std::string& path) -> std::vector<char> {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.is_open()) {
            LOG_ERROR("Vulkan", std::string("シェーダーファイルが開けません: ") + path);
            return {};
        }
        size_t size = static_cast<size_t>(f.tellg());
        f.seekg(0, std::ios::beg);
        std::vector<char> data(size);
        f.read(data.data(), size);
        return data;
    };

    auto vertSpv = loadSpv("Assets/Shader/shader.vert.spv");
    auto fragSpv = loadSpv("Assets/Shader/shader.frag.spv");
    if (vertSpv.empty() || fragSpv.empty()) return;

    vk::ShaderModuleCreateInfo vertInfo;
    vertInfo.codeSize = vertSpv.size();
    vertInfo.pCode    = reinterpret_cast<const uint32_t*>(vertSpv.data());
    vertShader = m_Device->createShaderModuleUnique(vertInfo);

    vk::ShaderModuleCreateInfo fragInfo;
    fragInfo.codeSize = fragSpv.size();
    fragInfo.pCode    = reinterpret_cast<const uint32_t*>(fragSpv.data());
    fragShader = m_Device->createShaderModuleUnique(fragInfo);

    vk::PipelineShaderStageCreateInfo shaderStages[2];
    shaderStages[0].stage  = vk::ShaderStageFlagBits::eVertex;
    shaderStages[0].module = vertShader.get();
    shaderStages[0].pName  = "main";
    shaderStages[1].stage  = vk::ShaderStageFlagBits::eFragment;
    shaderStages[1].module = fragShader.get();
    shaderStages[1].pName  = "main";

    vk::GraphicsPipelineCreateInfo pipelineInfo;
    pipelineInfo.pViewportState     = &viewportState;
    pipelineInfo.pVertexInputState  = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState  = &multisample;
    pipelineInfo.pColorBlendState   = &blend;
    pipelineInfo.layout             = pipelineLayout.get();
    pipelineInfo.renderPass         = m_Renderpass.get();
    pipelineInfo.subpass            = 0;
    pipelineInfo.stageCount         = 2;
    pipelineInfo.pStages            = shaderStages;

    m_Pipeline = m_Device->createGraphicsPipelineUnique(nullptr, pipelineInfo).value;

    // イメージビューとフレームバッファの作成
    m_swapchainImageViews.resize(swapchainImages.size());
    m_swapchainFramebufs.resize(swapchainImages.size());

    for (size_t i = 0; i < swapchainImages.size(); i++) {
        vk::ImageViewCreateInfo ivInfo;
        ivInfo.image                           = swapchainImages[i];
        ivInfo.viewType                        = vk::ImageViewType::e2D;
        ivInfo.format                          = swapchainFormat.format;
        ivInfo.components.r                    = vk::ComponentSwizzle::eIdentity;
        ivInfo.components.g                    = vk::ComponentSwizzle::eIdentity;
        ivInfo.components.b                    = vk::ComponentSwizzle::eIdentity;
        ivInfo.components.a                    = vk::ComponentSwizzle::eIdentity;
        ivInfo.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eColor;
        ivInfo.subresourceRange.baseMipLevel   = 0;
        ivInfo.subresourceRange.levelCount     = 1;
        ivInfo.subresourceRange.baseArrayLayer = 0;
        ivInfo.subresourceRange.layerCount     = 1;
        m_swapchainImageViews[i] = m_Device->createImageViewUnique(ivInfo);

        vk::ImageView attachments[] = { m_swapchainImageViews[i].get() };
        vk::FramebufferCreateInfo fbInfo;
        fbInfo.renderPass      = m_Renderpass.get();
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments    = attachments;
        fbInfo.width           = surfaceCaps.currentExtent.width;
        fbInfo.height          = surfaceCaps.currentExtent.height;
        fbInfo.layers          = 1;
        m_swapchainFramebufs[i] = m_Device->createFramebufferUnique(fbInfo);
    }
}

// =-=-= Shutdown =-=-=

void Vulkan::VulkanRenderer::Shutdown()
{
#if defined(_DEBUG)
    if (m_debugMessenger != VK_NULL_HANDLE && m_pfnDestroyDebugMessenger) {
        m_pfnDestroyDebugMessenger(
            static_cast<VkInstance>(m_Instance.get()), m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
#endif

    if (m_Device) {
        m_Device->waitIdle();

        if (pUniformBufMem2) {
            m_Device->unmapMemory(uniformBufMemory2.get());
            pUniformBufMem2 = nullptr;
        }
        if (pUniformBufMem) {
            m_Device->unmapMemory(uniformBufMemory.get());
            pUniformBufMem = nullptr;
        }

        descSets2.clear();
        descSets.clear();
        m_CmdBufs.clear();
        m_swapchainImageViews.clear();
        m_swapchainFramebufs.clear();
    }

    delete m_pCmdBufWrapper;
    m_pCmdBufWrapper = nullptr;

    LOG_INFO("Vulkan", "シャットダウン完了。");
}

// =-=-= BeginFrame =-=-=

void Vulkan::VulkanRenderer::BeginFrame()
{
    // アニメーション更新
    static float s_angle = 0.0f;
    static auto  s_lastTime = std::chrono::high_resolution_clock::now();

    auto now   = std::chrono::high_resolution_clock::now();
    float delta = std::chrono::duration<float, std::milli>(now - s_lastTime).count();
    s_lastTime  = now;
    s_angle    += delta;

    s_sceneData1.rectCenter.x = std::sinf(s_angle * 0.002f) * 0.5f;
    s_sceneData1.rectCenter.y = std::cosf(s_angle * 0.002f) * 0.5f;

    // 前フレームの完了を待機
    m_Device->waitForFences({imgRenderedFence.get()}, VK_TRUE, UINT64_MAX);

    // 次のスワップチェーンイメージを取得
    auto acquireResult = m_Device->acquireNextImageKHR(
        m_Swapchain.get(), 1'000'000'000, m_swapchainImgSemaphore.get());

    if (acquireResult.result == vk::Result::eSuboptimalKHR ||
        acquireResult.result == vk::Result::eErrorOutOfDateKHR) {
        m_Device->waitIdle();
        RecreateSwapchain();
        BeginFrame();
        return;
    }
    if (acquireResult.result != vk::Result::eSuccess) {
        LOG_ERROR("Vulkan", "イメージ取得失敗。");
        return;
    }

    m_Device->resetFences({imgRenderedFence.get()});
    m_currentImageIndex = acquireResult.value;

    // ユニフォームバッファの更新
    if (pUniformBufMem)  std::memcpy(pUniformBufMem,  &s_sceneData1, sizeof(SceneData));
    if (pUniformBufMem2) std::memcpy(pUniformBufMem2, &s_sceneData2, sizeof(SceneData));

    m_frameBegan = true;
}

// =-=-= BeginCommandBuffer =-=-=

Render::ICommandBuffer* Vulkan::VulkanRenderer::BeginCommandBuffer()
{
    if (!m_frameBegan || !m_pCmdBufWrapper) return nullptr;

    m_pCmdBufWrapper->Setup(m_CmdBufs[0].get(), this, m_currentImageIndex);
    return m_pCmdBufWrapper;
}

// =-=-= Submit =-=-=

void Vulkan::VulkanRenderer::Submit(Render::ICommandBuffer* /*cmd*/)
{
    if (!m_frameBegan) return;

    vk::Semaphore waitSemaphores[]   = { m_swapchainImgSemaphore.get() };
    vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
    vk::Semaphore signalSemaphores[] = { imgRenderedSemaphore.get() };
    vk::CommandBuffer submitBuf      = m_CmdBufs[0].get();

    vk::SubmitInfo submitInfo;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = waitSemaphores;
    submitInfo.pWaitDstStageMask    = waitStages;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &submitBuf;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = signalSemaphores;

    m_graphicsQueue.submit({submitInfo}, imgRenderedFence.get());
}

// =-=-= EndFrame =-=-=

void Vulkan::VulkanRenderer::EndFrame()
{
    if (!m_frameBegan) return;
    m_frameBegan = false;

    vk::Semaphore waitSemaphores[] = { imgRenderedSemaphore.get() };

    vk::PresentInfoKHR presentInfo;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = waitSemaphores;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &m_Swapchain.get();
    presentInfo.pImageIndices      = &m_currentImageIndex;

    auto result = m_graphicsQueue.presentKHR(presentInfo);
    if (result == vk::Result::eSuboptimalKHR ||
        result == vk::Result::eErrorOutOfDateKHR) {
        m_Device->waitIdle();
        RecreateSwapchain();
    }
}

// =-=-= DLL エクスポート関数 =-=-=

extern "C"
{
    __declspec(dllexport)
    Render::IRenderer* CreateRenderer(Render::GraphicsAPI /*api*/)
    {
        return new Vulkan::VulkanRenderer();
    }

    __declspec(dllexport)
    void DestroyRenderer(Render::IRenderer* renderer)
    {
        delete renderer;
    }
}
