#include "vulkan_rhi.h"
#include <string>
#include "../vk_engine.h"
#include "../tracy_vulkan.h"
#include <cstring>

VulkanRHI::VulkanRHI(VulkanEngine* engine) : _engine(engine) {}

VulkanRHI::~VulkanRHI() {
    Shutdown();
}
static bool create_gpu_buffer_rhi(struct VulkanEngine* engine, VkDeviceSize size, VkBufferUsageFlags usage, const void* srcData, VkBuffer& buf, VmaAllocation& alloc, const char* name) {
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation stgAlloc = VK_NULL_HANDLE;
    VkBufferCreateInfo stgIn{};
    stgIn.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stgIn.size = size;
    stgIn.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo stgAl{};
    stgAl.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    if (vmaCreateBuffer(engine->allocator, &stgIn, &stgAl, &staging, &stgAlloc, nullptr) != VK_SUCCESS) {
        return false;
    }
    {
        const std::string stagingBufferName = std::string(name) + "_Staging_Buffer";
        vk_set_object_name(engine->device, (uint64_t)staging, VK_OBJECT_TYPE_BUFFER, stagingBufferName.c_str());
    }

    void* map = nullptr;
    if (vmaMapMemory(engine->allocator, stgAlloc, &map) != VK_SUCCESS) {
        vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
        return false;
    }
    memcpy(map, srcData, size);
    vmaUnmapMemory(engine->allocator, stgAlloc);

    VkBufferCreateInfo gpuIn{};
    gpuIn.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    gpuIn.size = size;
    gpuIn.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo gpuAl{};
    gpuAl.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateBuffer(engine->allocator, &gpuIn, &gpuAl, &buf, &alloc, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)buf, VK_OBJECT_TYPE_BUFFER, name);

    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = engine->commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer stagingCb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(engine->device, &ai, &stagingCb) != VK_SUCCESS) {
        vmaDestroyBuffer(engine->allocator, buf, alloc);
        buf = VK_NULL_HANDLE;
        alloc = VK_NULL_HANDLE;
        vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
        return false;
    }
    {
        const std::string stagingCbName = std::string(name) + "_Staging_CommandBuffer";
        vk_set_object_name(engine->device, (uint64_t)stagingCb, VK_OBJECT_TYPE_COMMAND_BUFFER, stagingCbName.c_str());
    }
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(stagingCb, &bi) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &stagingCb);
        vmaDestroyBuffer(engine->allocator, buf, alloc);
        buf = VK_NULL_HANDLE;
        alloc = VK_NULL_HANDLE;
        vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
        return false;
    }

    vk_begin_label(engine->device, stagingCb, "GPU_Staging_Copy", 0.0f, 1.0f, 0.0f);
    VkBufferCopy cp{};
    cp.size = size;
    vkCmdCopyBuffer(stagingCb, staging, buf, 1, &cp);
    vk_end_label(engine->device, stagingCb);

    if (vkEndCommandBuffer(stagingCb) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &stagingCb);
        vmaDestroyBuffer(engine->allocator, buf, alloc);
        buf = VK_NULL_HANDLE;
        alloc = VK_NULL_HANDLE;
        vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
        return false;
    }

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &stagingCb;

    if (vkQueueSubmit(engine->graphicsQueue, 1, &si, nullptr) != VK_SUCCESS || vkQueueWaitIdle(engine->graphicsQueue) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &stagingCb);
        vmaDestroyBuffer(engine->allocator, buf, alloc);
        buf = VK_NULL_HANDLE;
        alloc = VK_NULL_HANDLE;
        vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
        return false;
    }

    vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &stagingCb);
    vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
    return true;
}


bool VulkanRHI::Init() {
    return true;
}

void VulkanRHI::Shutdown() {
}

BufferHandle VulkanRHI::CreateBuffer(std::size_t size, BufferUsage usage, const void* initialData, const char* name) {
    VkBufferUsageFlags vkUsage = 0;
    switch (usage) {
    case BufferUsage::Vertex: vkUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; break;
    case BufferUsage::Index: vkUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT; break;
    case BufferUsage::Uniform: vkUsage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; break;
    case BufferUsage::Storage: vkUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; break;
    }

    VulkanBuffer buf{};
    if (initialData) {
        if (!create_gpu_buffer_rhi(_engine, size, vkUsage, initialData, buf.buffer, buf.allocation, name ? name : "Buffer")) {
            return INVALID_HANDLE;
        }
    } else {
        // Needs CPU mapping for uniform/storage (we'll assume anything without initialData is mapped)
        VkBufferCreateInfo in{};
        in.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        in.size = size;
        in.usage = vkUsage;
        VmaAllocationCreateInfo al{};
        al.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        al.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo allocInfo;
        if (vmaCreateBuffer(_engine->allocator, &in, &al, &buf.buffer, &buf.allocation, &allocInfo) != VK_SUCCESS) {
            return INVALID_HANDLE;
        }
        buf.mappedData = allocInfo.pMappedData;
        if (name) vk_set_object_name(_engine->device, (uint64_t)buf.buffer, VK_OBJECT_TYPE_BUFFER, name);
    }

    uint32_t handle = m_nextBufferHandle++;
    if (handle >= m_buffers.size()) {
        m_buffers.resize(handle + 1);
    }
    m_buffers[handle] = buf;
    return handle;
}

void VulkanRHI::DestroyBuffer(BufferHandle handle) {
    if (handle != INVALID_HANDLE && handle < m_buffers.size()) {
        auto& buf = m_buffers[handle];
        if (buf.buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(_engine->allocator, buf.buffer, buf.allocation);
            buf.buffer = VK_NULL_HANDLE;
            buf.allocation = VK_NULL_HANDLE;
            buf.mappedData = nullptr;
        }
    }
}

void* VulkanRHI::MapBuffer(BufferHandle handle) {
    if (handle == INVALID_HANDLE || handle >= m_buffers.size() || !m_buffers[handle].buffer) {
        return nullptr;
    }
    // If it was created mapped, just return the mapped pointer
    if (m_buffers[handle].mappedData) {
        return m_buffers[handle].mappedData;
    }
    void* mapped = nullptr;
    vmaMapMemory(_engine->allocator, m_buffers[handle].allocation, &mapped);
    return mapped;
}

void VulkanRHI::UnmapBuffer(BufferHandle handle) {
    if (handle != INVALID_HANDLE && handle < m_buffers.size() && m_buffers[handle].buffer) {
        // If it was created mapped, we don't unmap it manually
        if (!m_buffers[handle].mappedData) {
            vmaUnmapMemory(_engine->allocator, m_buffers[handle].allocation);
        }
    }
}

VkBuffer VulkanRHI::GetVkBuffer(BufferHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_buffers.size()) {
        return m_buffers[handle].buffer;
    }
    return VK_NULL_HANDLE;
}

TextureHandle VulkanRHI::CreateTexture(uint32_t width, uint32_t height, TextureFormat format, TextureUsage usage, uint32_t mipLevels, const char* name) {
    VulkanTexture tex{};
    
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.extent.width = width;
    ii.extent.height = height;
    ii.extent.depth = 1;
    ii.mipLevels = mipLevels;
    ii.arrayLayers = 1;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    
    switch (format) {
        case TextureFormat::RGBA8_UNORM: ii.format = VK_FORMAT_R8G8B8A8_UNORM; break;
        case TextureFormat::RGBA32_SFLOAT: ii.format = VK_FORMAT_R32G32B32A32_SFLOAT; break;
        case TextureFormat::RGBA16_SFLOAT: ii.format = VK_FORMAT_R16G16B16A16_SFLOAT; break;
        case TextureFormat::RG16_SFLOAT: ii.format = VK_FORMAT_R16G16_SFLOAT; break;
        case TextureFormat::Depth: ii.format = _engine->depthFormat; break;
        default: ii.format = VK_FORMAT_R8G8B8A8_UNORM; break;
    }

    switch (usage) {
        case TextureUsage::Sampled: ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; break;
        case TextureUsage::DepthAttachment: ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; break;
        case TextureUsage::ColorAttachment: ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; break;
        case TextureUsage::Storage: ii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; break;
    }

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateImage(_engine->allocator, &ii, &ai, &tex.image, &tex.allocation, nullptr) != VK_SUCCESS) {
        return INVALID_HANDLE;
    }
    
    if (name) vk_set_object_name(_engine->device, (uint64_t)tex.image, VK_OBJECT_TYPE_IMAGE, name);

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = tex.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = ii.format;
    if (format == TextureFormat::Depth) {
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    } else {
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    vi.subresourceRange.levelCount = mipLevels;
    vi.subresourceRange.layerCount = 1;

    if (vkCreateImageView(_engine->device, &vi, nullptr, &tex.imageView) != VK_SUCCESS) {
        vmaDestroyImage(_engine->allocator, tex.image, tex.allocation);
        return INVALID_HANDLE;
    }
    
    if (name) {
        std::string viewName = std::string(name) + "_View";
        vk_set_object_name(_engine->device, (uint64_t)tex.imageView, VK_OBJECT_TYPE_IMAGE_VIEW, viewName.c_str());
    }

    uint32_t handle = m_nextTextureHandle++;
    if (handle >= m_textures.size()) {
        m_textures.resize(handle + 1);
    }
    m_textures[handle] = tex;
    return handle;
}

void VulkanRHI::DestroyTexture(TextureHandle handle) {
    if (handle != INVALID_HANDLE && handle < m_textures.size()) {
        auto& tex = m_textures[handle];
        if (tex.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(_engine->device, tex.imageView, nullptr);
            tex.imageView = VK_NULL_HANDLE;
        }
        if (tex.image != VK_NULL_HANDLE) {
            vmaDestroyImage(_engine->allocator, tex.image, tex.allocation);
            tex.image = VK_NULL_HANDLE;
            tex.allocation = VK_NULL_HANDLE;
        }
    }
}

SamplerHandle VulkanRHI::CreateSampler(uint32_t mipLevels, bool clampToEdge, const char* name) {
    VulkanSampler samp{};
    
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = clampToEdge ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeV = clampToEdge ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeW = clampToEdge ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.anisotropyEnable = VK_FALSE;
    si.maxAnisotropy = 1.0f;
    si.compareEnable = VK_FALSE;
    si.compareOp = VK_COMPARE_OP_ALWAYS;
    si.minLod = 0.0f;
    si.maxLod = static_cast<float>(mipLevels > 0 ? mipLevels - 1 : 0);
    si.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    si.unnormalizedCoordinates = VK_FALSE;

    if (vkCreateSampler(_engine->device, &si, nullptr, &samp.sampler) != VK_SUCCESS) {
        return INVALID_HANDLE;
    }
    if (name) vk_set_object_name(_engine->device, (uint64_t)samp.sampler, VK_OBJECT_TYPE_SAMPLER, name);

    uint32_t handle = m_nextSamplerHandle++;
    if (handle >= m_samplers.size()) {
        m_samplers.resize(handle + 1);
    }
    m_samplers[handle] = samp;
    return handle;
}

void VulkanRHI::DestroySampler(SamplerHandle handle) {
    if (handle != INVALID_HANDLE && handle < m_samplers.size()) {
        auto& samp = m_samplers[handle];
        if (samp.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(_engine->device, samp.sampler, nullptr);
            samp.sampler = VK_NULL_HANDLE;
        }
    }
}

VkImage VulkanRHI::GetVkImage(TextureHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_textures.size()) return m_textures[handle].image;
    return VK_NULL_HANDLE;
}

VkImageView VulkanRHI::GetVkImageView(TextureHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_textures.size()) return m_textures[handle].imageView;
    return VK_NULL_HANDLE;
}

VkSampler VulkanRHI::GetVkSampler(SamplerHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_samplers.size()) return m_samplers[handle].sampler;
    return VK_NULL_HANDLE;
}

SwapchainStatus VulkanRHI::AcquireNextImage(uint32_t* imageIndex) {
    if (vkWaitForFences(_engine->device, 1, &_engine->inFlightFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        return SwapchainStatus::Error;
    }
    if (vkResetFences(_engine->device, 1, &_engine->inFlightFence) != VK_SUCCESS) {
        return SwapchainStatus::Error;
    }

    VkResult acquireResult = vkAcquireNextImageKHR(_engine->device, _engine->swapchain, UINT64_MAX, _engine->imageAvailableSemaphore, nullptr, imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        return SwapchainStatus::NeedRecreate;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        return SwapchainStatus::Error;
    }
    return SwapchainStatus::Ok;
}

void VulkanRHI::UpdateUBO(const UBOData& data) {
    memcpy(_engine->uniformBufferMapped, &data, sizeof(UBOData));
}

SwapchainStatus VulkanRHI::SubmitAndPresent(uint32_t imageIndex) {
    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &_engine->imageAvailableSemaphore;
    si.pWaitDstStageMask = &wait;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &_engine->commandBuffer;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &_engine->renderFinishedSemaphore;
    if (vkQueueSubmit(_engine->graphicsQueue, 1, &si, _engine->inFlightFence) != VK_SUCCESS) {
        return SwapchainStatus::Error;
    }
    VkPresentInfoKHR pri{};
    pri.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pri.waitSemaphoreCount = 1;
    pri.pWaitSemaphores = &_engine->renderFinishedSemaphore;
    pri.swapchainCount = 1;
    pri.pSwapchains = &_engine->swapchain;
    pri.pImageIndices = &imageIndex;
    VkResult presentResult = vkQueuePresentKHR(_engine->presentQueue, &pri);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        return SwapchainStatus::NeedRecreate;
    }
    return presentResult == VK_SUCCESS ? SwapchainStatus::Ok : SwapchainStatus::Error;
}

bool VulkanRHI::BeginFrame() {
    if (vkResetCommandBuffer(_engine->commandBuffer, 0) != VK_SUCCESS) {
        return false;
    }
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    return vkBeginCommandBuffer(_engine->commandBuffer, &bi) == VK_SUCCESS;
}

void VulkanRHI::EndFrame() {
    vkEndCommandBuffer(_engine->commandBuffer);
}

void VulkanRHI::BeginRenderPass() {
    VkClearValue cl[2] = {};
    cl[0].color = {{0.05f, 0.05f, 0.2f, 1.0f}};
    cl[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = _engine->renderPass;
    rp.framebuffer = _engine->swapchainFramebuffers[_engine->lastRenderedImageIndex];
    rp.renderArea.extent = _engine->swapchainExtent;
    rp.clearValueCount = 2;
    rp.pClearValues = cl;

    vkCmdBeginRenderPass(_engine->commandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanRHI::EndRenderPass() {
    vkCmdEndRenderPass(_engine->commandBuffer);
}

void VulkanRHI::BindPipeline(PipelineType type) {
    VkPipeline pipeline = VK_NULL_HANDLE;
    switch (type) {
    case PipelineType::Graphics:
        pipeline = _engine->graphicsPipeline;
        break;
    case PipelineType::Billboard:
        pipeline = _engine->billboardPipeline;
        break;
    case PipelineType::Wireframe:
        pipeline = _engine->wireframePipeline;
        break;
    case PipelineType::DebugLine:
        pipeline = _engine->debugLinePipeline;
        break;
    case PipelineType::DebugTriangle:
        pipeline = _engine->debugTrianglePipeline;
        break;
    case PipelineType::Skybox:
        pipeline = _engine->skyboxPipeline;
        break;
    }
    if (pipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(_engine->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    }
}

void VulkanRHI::BindGlobalDescriptor() {
    vkCmdBindDescriptorSets(_engine->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _engine->pipelineLayout, 0, 1, &_engine->descriptorSet, 0, nullptr);
}

void VulkanRHI::BindMeshBuffers(bool isBillboard) {
    if (isBillboard) {
        if (_engine->billboardBuffer != INVALID_HANDLE && _engine->billboardBuffer < m_buffers.size()) {
            VkBuffer buffers[] = {m_buffers[_engine->billboardBuffer].buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(_engine->commandBuffer, 1, 1, buffers, offsets);
        }
    } else {
        if (_engine->vertexBuffer != INVALID_HANDLE && _engine->instanceBuffer != INVALID_HANDLE) {
            VkBuffer buffers[] = {m_buffers[_engine->vertexBuffer].buffer, m_buffers[_engine->instanceBuffer].buffer};
            VkDeviceSize offsets[] = {0, 0};
            vkCmdBindVertexBuffers(_engine->commandBuffer, 0, 2, buffers, offsets);
        }
        if (_engine->indexBuffer != INVALID_HANDLE) {
            vkCmdBindIndexBuffer(_engine->commandBuffer, m_buffers[_engine->indexBuffer].buffer, 0, VK_INDEX_TYPE_UINT32);
        }
    }
}

void VulkanRHI::Draw(uint32_t vertexCount, uint32_t instanceCount) {
    vkCmdDraw(_engine->commandBuffer, vertexCount, instanceCount, 0, 0);
}

void VulkanRHI::DrawIndexed(uint32_t indexCount, uint32_t instanceCount) {
    vkCmdDrawIndexed(_engine->commandBuffer, indexCount, instanceCount, 0, 0, 0);
}

void VulkanRHI::PushDebugConstants(const void* data, uint32_t size) {
    vkCmdPushConstants(_engine->commandBuffer, _engine->debugPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, size, data);
}

void VulkanRHI::UpdateBillboardInstances(const struct BillboardInstance* instances, std::size_t count) {
    if (_engine->billboardBuffer != INVALID_HANDLE && _engine->billboardBuffer < m_buffers.size()) {
        void* mapped = m_buffers[_engine->billboardBuffer].mappedData;
        if (mapped) {
            memcpy(mapped, instances, count * sizeof(BillboardInstance));
        }
    }
}

void VulkanRHI::BeginDebugLabel(const char* name, float r, float g, float b) {
    vk_begin_label(_engine->device, _engine->commandBuffer, name, r, g, b);
}

void VulkanRHI::EndDebugLabel() {
    vk_end_label(_engine->device, _engine->commandBuffer);
}

void VulkanRHI::CollectProfiling() {
    tracy_vk_collect(_engine, _engine->commandBuffer);
}

void VulkanRHI::GetResolution(uint32_t* width, uint32_t* height) const {
    *width = _engine->swapchainExtent.width;
    *height = _engine->swapchainExtent.height;
}

void* VulkanRHI::GetOpaqueTracyContext() const {
    return _engine->tracyVkContext;
}

void* VulkanRHI::GetOpaqueCommandBuffer() const {
    return _engine->commandBuffer;
}

