#include "vulkan_command_list.h"
#include "vulkan_rhi.h"
#include <string>
#include "../vk_engine.h"
#include "../tracy_vulkan.h"
#include <cstring>

#include "vk_engine.h"
#include "../app_log.h"
#include <new>

extern "C" {
    __attribute__((visibility("default"))) IRHI* CreateRHI(EngineState* state) {
        state->rhiArena.offset = 0; // Reset arena on load
        
        void* engineMem = arena_alloc(&state->rhiArena, sizeof(VulkanEngine), alignof(VulkanEngine));
        VulkanEngine* engine = new (engineMem) VulkanEngine();
        engine->appState = state;
        
        void* rhiMem = arena_alloc(&state->rhiArena, sizeof(VulkanRHI), alignof(VulkanRHI));
        return new (rhiMem) VulkanRHI(engine);
    }
    
    __attribute__((visibility("default"))) void DestroyRHI(IRHI* rhi) {
        if (!rhi) return;
        VulkanRHI* vkRhi = static_cast<VulkanRHI*>(rhi);
        VulkanEngine* engine = vkRhi->_engine;
        vkRhi->~VulkanRHI();
        if (engine) {
            engine->~VulkanEngine();
        }
        // Memory is explicitly left in the arena; reclaimed on next CreateRHI or shutdown.
    }
}

VulkanRHI::VulkanRHI(VulkanEngine* engine) : _engine(engine) {}

VulkanRHI::~VulkanRHI() {
    delete m_mainCmdList;
    m_mainCmdList = nullptr;
    Shutdown();
}
static RHIResult create_gpu_buffer_rhi(struct VulkanEngine* engine, VkDeviceSize size, VkBufferUsageFlags usage, const void* srcData, VkBuffer& buf, VmaAllocation& alloc, const char* name) {
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation stgAlloc = VK_NULL_HANDLE;
    VkBufferCreateInfo stgIn{};
    stgIn.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stgIn.size = size;
    stgIn.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo stgAl{};
    stgAl.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    if (vmaCreateBuffer(engine->ctx.allocator, &stgIn, &stgAl, &staging, &stgAlloc, nullptr) != VK_SUCCESS) {
        return RHIResult::ErrorInitializationFailed;
    }
    {
        const char* stagingBufferName = log_format("%s_Staging_Buffer", name);
        vk_set_object_name(engine->ctx.device, (uint64_t)staging, VK_OBJECT_TYPE_BUFFER, stagingBufferName);
    }

    void* map = nullptr;
    if (vmaMapMemory(engine->ctx.allocator, stgAlloc, &map) != VK_SUCCESS) {
        vmaDestroyBuffer(engine->ctx.allocator, staging, stgAlloc);
        return RHIResult::ErrorInitializationFailed;
    }
    memcpy(map, srcData, size);
    vmaUnmapMemory(engine->ctx.allocator, stgAlloc);

    VkBufferCreateInfo gpuIn{};
    gpuIn.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    gpuIn.size = size;
    gpuIn.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo gpuAl{};
    gpuAl.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateBuffer(engine->ctx.allocator, &gpuIn, &gpuAl, &buf, &alloc, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(engine->ctx.allocator, staging, stgAlloc);
        return RHIResult::ErrorInitializationFailed;
    }
    vk_set_object_name(engine->ctx.device, (uint64_t)buf, VK_OBJECT_TYPE_BUFFER, name);

    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = engine->ctx.commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer stagingCb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(engine->ctx.device, &ai, &stagingCb) != VK_SUCCESS) {
        vmaDestroyBuffer(engine->ctx.allocator, buf, alloc);
        buf = VK_NULL_HANDLE;
        alloc = VK_NULL_HANDLE;
        vmaDestroyBuffer(engine->ctx.allocator, staging, stgAlloc);
        return RHIResult::ErrorInitializationFailed;
    }
    {
        const char* stagingCbName = log_format("%s_Staging_CommandBuffer", name);
        vk_set_object_name(engine->ctx.device, (uint64_t)stagingCb, VK_OBJECT_TYPE_COMMAND_BUFFER, stagingCbName);
    }
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(stagingCb, &bi) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->ctx.device, engine->ctx.commandPool, 1, &stagingCb);
        vmaDestroyBuffer(engine->ctx.allocator, buf, alloc);
        buf = VK_NULL_HANDLE;
        alloc = VK_NULL_HANDLE;
        vmaDestroyBuffer(engine->ctx.allocator, staging, stgAlloc);
        return RHIResult::ErrorInitializationFailed;
    }

    vk_begin_label(engine->ctx.device, stagingCb, "GPU_Staging_Copy", 0.0f, 1.0f, 0.0f);
    VkBufferCopy cp{};
    cp.size = size;
    vkCmdCopyBuffer(stagingCb, staging, buf, 1, &cp);
    vk_end_label(engine->ctx.device, stagingCb);

    if (vkEndCommandBuffer(stagingCb) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->ctx.device, engine->ctx.commandPool, 1, &stagingCb);
        vmaDestroyBuffer(engine->ctx.allocator, buf, alloc);
        buf = VK_NULL_HANDLE;
        alloc = VK_NULL_HANDLE;
        vmaDestroyBuffer(engine->ctx.allocator, staging, stgAlloc);
        return RHIResult::ErrorInitializationFailed;
    }

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &stagingCb;

    if (vkQueueSubmit(engine->ctx.graphicsQueue, 1, &si, nullptr) != VK_SUCCESS || vkQueueWaitIdle(engine->ctx.graphicsQueue) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->ctx.device, engine->ctx.commandPool, 1, &stagingCb);
        vmaDestroyBuffer(engine->ctx.allocator, buf, alloc);
        buf = VK_NULL_HANDLE;
        alloc = VK_NULL_HANDLE;
        vmaDestroyBuffer(engine->ctx.allocator, staging, stgAlloc);
        return RHIResult::ErrorInitializationFailed;
    }

    vkFreeCommandBuffers(engine->ctx.device, engine->ctx.commandPool, 1, &stagingCb);
    vmaDestroyBuffer(engine->ctx.allocator, staging, stgAlloc);
    return RHIResult::Success;
}


RHIResult VulkanRHI::Init() {
    return init_vulkan_engine(_engine) == GfxResult::Success ? RHIResult::Success : RHIResult::ErrorInitializationFailed;
}

void VulkanRHI::Shutdown() {
    if (m_isShutdown || !_engine) {
        return;
    }
    m_isShutdown = true;
    delete m_mainCmdList;
    m_mainCmdList = nullptr;

    if (_engine->ctx.device != VK_NULL_HANDLE) {
        m_descriptorCache.Cleanup(_engine->ctx.device);
        m_descriptorAllocator.Cleanup(_engine->ctx.device);
    }
    cleanup_vulkan_engine(_engine);
}

#include "vk_engine_runtime.h"

RHIResult VulkanRHI::DrawFrame() {
    return draw_frame(_engine) == GfxResult::Success ? RHIResult::Success : RHIResult::ErrorInitializationFailed;
}

void VulkanRHI::HandleInputs(const struct WindowOps* ops) {
    vk_handle_runtime_input(_engine, ops);
    vk_update_camera_key_state(_engine, ops);
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
        if (create_gpu_buffer_rhi(_engine, size, vkUsage, initialData, buf.buffer, buf.allocation, name ? name : "Buffer") != RHIResult::Success) {
            return INVALID_HANDLE;
        }
    } else {
        // Needs CPU mapping for uniform/storage (we'll assume anything without initialData is mapped)
        VkBufferCreateInfo in{};
        in.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        in.size = size;
        in.usage = vkUsage;
        VmaAllocationCreateInfo al{};
        al.usage = VMA_MEMORY_USAGE_AUTO;
        al.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        VmaAllocationInfo allocInfo;
        if (vmaCreateBuffer(_engine->ctx.allocator, &in, &al, &buf.buffer, &buf.allocation, &allocInfo) != VK_SUCCESS) {
            return INVALID_HANDLE;
        }
        buf.mappedData = allocInfo.pMappedData;
    }

    buf.size = size;
    buf.currentState = ResourceState::Undefined;

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
            if (_engine && _engine->ctx.allocator != VK_NULL_HANDLE) {
                vmaDestroyBuffer(_engine->ctx.allocator, buf.buffer, buf.allocation);
            }
            buf.buffer = VK_NULL_HANDLE;
            buf.allocation = VK_NULL_HANDLE;
            buf.mappedData = nullptr;
            buf.size = 0;
            buf.currentState = ResourceState::Undefined;
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
    vmaMapMemory(_engine->ctx.allocator, m_buffers[handle].allocation, &mapped);
    return mapped;
}

void VulkanRHI::UnmapBuffer(BufferHandle handle) {
    if (handle != INVALID_HANDLE && handle < m_buffers.size() && m_buffers[handle].buffer) {
        // If it was created mapped, we don't unmap it manually
        if (!m_buffers[handle].mappedData) {
            vmaUnmapMemory(_engine->ctx.allocator, m_buffers[handle].allocation);
        }
    }
}

VkBuffer VulkanRHI::GetVkBuffer(BufferHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_buffers.size()) {
        return m_buffers[handle].buffer;
    }
    return VK_NULL_HANDLE;
}

ResourceState VulkanRHI::GetTextureState(TextureHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_textures.size()) {
        return m_textures[handle].currentState;
    }
    return ResourceState::Undefined;
}

void VulkanRHI::SetTextureState(TextureHandle handle, ResourceState state) {
    if (handle != INVALID_HANDLE && handle < m_textures.size()) {
        m_textures[handle].currentState = state;
    }
}

VkFormat VulkanRHI::GetTextureVkFormat(TextureHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_textures.size()) {
        return m_textures[handle].format;
    }
    return VK_FORMAT_UNDEFINED;
}

uint32_t VulkanRHI::GetTextureMipLevels(TextureHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_textures.size()) {
        return m_textures[handle].mipLevels;
    }
    return 1;
}

bool VulkanRHI::IsTextureDepth(TextureHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_textures.size()) {
        return m_textures[handle].isDepth;
    }
    return false;
}

ResourceState VulkanRHI::GetBufferState(BufferHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_buffers.size()) {
        return m_buffers[handle].currentState;
    }
    return ResourceState::Undefined;
}

void VulkanRHI::SetBufferState(BufferHandle handle, ResourceState state) {
    if (handle != INVALID_HANDLE && handle < m_buffers.size()) {
        m_buffers[handle].currentState = state;
    }
}

std::size_t VulkanRHI::GetBufferSize(BufferHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_buffers.size()) {
        return m_buffers[handle].size;
    }
    return 0;
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
    case TextureFormat::BGRA8_UNORM: ii.format = VK_FORMAT_B8G8R8A8_UNORM; break;
    case TextureFormat::RGBA32_SFLOAT: ii.format = VK_FORMAT_R32G32B32A32_SFLOAT; break;
    case TextureFormat::RGBA16_SFLOAT: ii.format = VK_FORMAT_R16G16B16A16_SFLOAT; break;
    case TextureFormat::RG16_SFLOAT: ii.format = VK_FORMAT_R16G16_SFLOAT; break;
    case TextureFormat::B10G11R11_UFLOAT: ii.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32; break;
    case TextureFormat::Depth: ii.format = _engine->swapchainMgr.depthFormat; break;
    default: ii.format = VK_FORMAT_R8G8B8A8_UNORM; break;
    }

    switch (usage) {
        case TextureUsage::Sampled: ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT; break;
        case TextureUsage::DepthAttachment: ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; break;
        case TextureUsage::ColorAttachment: ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; break;
        case TextureUsage::Storage: ii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; break;
    }

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateImage(_engine->ctx.allocator, &ii, &ai, &tex.image, &tex.allocation, nullptr) != VK_SUCCESS) {
        return INVALID_HANDLE;
    }
    tex.format = ii.format;
    tex.width = width;
    tex.height = height;
    tex.mipLevels = mipLevels;
    tex.isDepth = (format == TextureFormat::Depth);
    tex.currentState = ResourceState::Undefined;
    
    if (name) vk_set_object_name(_engine->ctx.device, (uint64_t)tex.image, VK_OBJECT_TYPE_IMAGE, name);

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

    if (vkCreateImageView(_engine->ctx.device, &vi, nullptr, &tex.imageView) != VK_SUCCESS) {
        vmaDestroyImage(_engine->ctx.allocator, tex.image, tex.allocation);
        return INVALID_HANDLE;
    }
    
    if (name) {
        const char* viewName = log_format("%s_View", name);
        vk_set_object_name(_engine->ctx.device, (uint64_t)tex.imageView, VK_OBJECT_TYPE_IMAGE_VIEW, viewName);
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
            if (_engine && _engine->ctx.device != VK_NULL_HANDLE) {
                vkDestroyImageView(_engine->ctx.device, tex.imageView, nullptr);
            }
            tex.imageView = VK_NULL_HANDLE;
        }
        if (tex.image != VK_NULL_HANDLE) {
            if (_engine && _engine->ctx.allocator != VK_NULL_HANDLE) {
                vmaDestroyImage(_engine->ctx.allocator, tex.image, tex.allocation);
            }
            tex.image = VK_NULL_HANDLE;
            tex.allocation = VK_NULL_HANDLE;
            tex.width = 0;
            tex.height = 0;
            tex.mipLevels = 0;
            tex.isDepth = false;
            tex.currentState = ResourceState::Undefined;
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

    if (vkCreateSampler(_engine->ctx.device, &si, nullptr, &samp.sampler) != VK_SUCCESS) {
        return INVALID_HANDLE;
    }
    if (name) vk_set_object_name(_engine->ctx.device, (uint64_t)samp.sampler, VK_OBJECT_TYPE_SAMPLER, name);

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
            if (_engine && _engine->ctx.device != VK_NULL_HANDLE) {
                vkDestroySampler(_engine->ctx.device, samp.sampler, nullptr);
            }
            samp.sampler = VK_NULL_HANDLE;
        }
    }
}

static VkDescriptorType MapDescriptorType(DescriptorType type) {
    switch (type) {
        case DescriptorType::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
        case DescriptorType::CombinedImageSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case DescriptorType::SampledImage: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case DescriptorType::StorageImage: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case DescriptorType::UniformTexelBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        case DescriptorType::StorageTexelBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        case DescriptorType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case DescriptorType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case DescriptorType::UniformBufferDynamic: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case DescriptorType::StorageBufferDynamic: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        case DescriptorType::InputAttachment: return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    }
    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

DescriptorLayoutHandle VulkanRHI::CreateDescriptorLayout(const DescriptorLayoutDesc& desc, const char* name) {
    VkDescriptorSetLayoutBinding* vkBindings = static_cast<VkDescriptorSetLayoutBinding*>(__builtin_alloca(desc.bindingCount * sizeof(VkDescriptorSetLayoutBinding)));
    for (uint32_t i = 0; i < desc.bindingCount; ++i) {
        vkBindings[i].binding = desc.bindings[i].binding;
        vkBindings[i].descriptorType = MapDescriptorType(desc.bindings[i].descriptorType);
        vkBindings[i].descriptorCount = desc.bindings[i].descriptorCount;
        vkBindings[i].stageFlags = static_cast<VkShaderStageFlags>(desc.bindings[i].stageFlags);
        vkBindings[i].pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = desc.bindingCount;
    info.pBindings = vkBindings;

    VulkanDescriptorLayout layout;
    if (vkCreateDescriptorSetLayout(_engine->ctx.device, &info, nullptr, &layout.layout) != VK_SUCCESS) {
        return INVALID_HANDLE;
    }

    if (name) vk_set_object_name(_engine->ctx.device, (uint64_t)layout.layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, name);

    uint32_t handle = m_nextDescriptorLayoutHandle++;
    if (handle >= m_descriptorLayouts.size()) {
        m_descriptorLayouts.resize(handle + 1);
    }
    m_descriptorLayouts[handle] = layout;
    return handle;
}

void VulkanRHI::DestroyDescriptorLayout(DescriptorLayoutHandle handle) {
    if (handle != INVALID_HANDLE && handle < m_descriptorLayouts.size()) {
        auto& layout = m_descriptorLayouts[handle];
        if (layout.layout != VK_NULL_HANDLE) {
            if (_engine && _engine->ctx.device != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(_engine->ctx.device, layout.layout, nullptr);
            }
            layout.layout = VK_NULL_HANDLE;
        }
    }
}

DescriptorPoolHandle VulkanRHI::CreateDescriptorPool(const DescriptorPoolDesc& desc, const char* name) {
    VkDescriptorPoolSize* vkPoolSizes = static_cast<VkDescriptorPoolSize*>(__builtin_alloca(desc.poolSizeCount * sizeof(VkDescriptorPoolSize)));
    for (uint32_t i = 0; i < desc.poolSizeCount; ++i) {
        vkPoolSizes[i].type = MapDescriptorType(desc.poolSizes[i].type);
        vkPoolSizes[i].descriptorCount = desc.poolSizes[i].descriptorCount;
    }

    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.poolSizeCount = desc.poolSizeCount;
    info.pPoolSizes = vkPoolSizes;
    info.maxSets = desc.maxSets;

    VulkanDescriptorPool pool;
    if (vkCreateDescriptorPool(_engine->ctx.device, &info, nullptr, &pool.pool) != VK_SUCCESS) {
        return INVALID_HANDLE;
    }

    if (name) vk_set_object_name(_engine->ctx.device, (uint64_t)pool.pool, VK_OBJECT_TYPE_DESCRIPTOR_POOL, name);

    uint32_t handle = m_nextDescriptorPoolHandle++;
    if (handle >= m_descriptorPools.size()) {
        m_descriptorPools.resize(handle + 1);
    }
    m_descriptorPools[handle] = pool;
    return handle;
}

void VulkanRHI::DestroyDescriptorPool(DescriptorPoolHandle handle) {
    if (handle != INVALID_HANDLE && handle < m_descriptorPools.size()) {
        auto& pool = m_descriptorPools[handle];
        if (pool.pool != VK_NULL_HANDLE) {
            if (_engine && _engine->ctx.device != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(_engine->ctx.device, pool.pool, nullptr);
            }
            pool.pool = VK_NULL_HANDLE;
        }
    }
}

RHIResult VulkanRHI::AllocateDescriptorSets(const DescriptorSetAllocateDesc& desc, DescriptorSetHandle* outSets) {
    if (desc.pool == INVALID_HANDLE || desc.pool >= m_descriptorPools.size()) return RHIResult::ErrorInitializationFailed;
    
    VkDescriptorSetLayout* vkLayouts = static_cast<VkDescriptorSetLayout*>(__builtin_alloca(desc.setCount * sizeof(VkDescriptorSetLayout)));
    for (uint32_t i = 0; i < desc.setCount; ++i) {
        if (desc.layouts[i] == INVALID_HANDLE || desc.layouts[i] >= m_descriptorLayouts.size()) return RHIResult::ErrorInitializationFailed;
        vkLayouts[i] = m_descriptorLayouts[desc.layouts[i]].layout;
    }

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_descriptorPools[desc.pool].pool;
    ai.descriptorSetCount = desc.setCount;
    ai.pSetLayouts = vkLayouts;

    VkDescriptorSet* vkSets = static_cast<VkDescriptorSet*>(__builtin_alloca(desc.setCount * sizeof(VkDescriptorSet)));
    if (vkAllocateDescriptorSets(_engine->ctx.device, &ai, vkSets) != VK_SUCCESS) {
        return RHIResult::ErrorOutOfMemory;
    }

    for (uint32_t i = 0; i < desc.setCount; ++i) {
        uint32_t handle = m_nextDescriptorSetHandle++;
        if (handle >= m_descriptorSets.size()) {
            m_descriptorSets.resize(handle + 1);
        }
        m_descriptorSets[handle] = {vkSets[i]};
        outSets[i] = handle;
    }
    return RHIResult::Success;
}

VkImageView VulkanRHI::GetVkImageViewForHandle(ImageViewHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_imageViews.size()) {
        return m_imageViews[handle].view;
    }
    return VK_NULL_HANDLE;
}

ImageViewHandle VulkanRHI::CreateImageView(TextureHandle texture, uint32_t baseMipLevel, uint32_t levelCount, uint32_t baseArrayLayer, uint32_t layerCount) {
    if (texture == INVALID_HANDLE || texture >= m_textures.size() || !m_textures[texture].image) return INVALID_HANDLE;
    
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = m_textures[texture].image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = m_textures[texture].format;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (vi.format == _engine->swapchainMgr.depthFormat) {
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    vi.subresourceRange.baseMipLevel = baseMipLevel;
    if (levelCount == (uint32_t)-1) {
        levelCount = 1; // Default to 1 level if not specified
    }
    vi.subresourceRange.levelCount = levelCount;
    vi.subresourceRange.baseArrayLayer = baseArrayLayer;
    vi.subresourceRange.layerCount = layerCount;

    VkImageView view;
    if (vkCreateImageView(_engine->ctx.device, &vi, nullptr, &view) != VK_SUCCESS) {
        return INVALID_HANDLE;
    }

    uint32_t handle = m_nextImageViewHandle++;
    if (handle >= m_imageViews.size()) {
        m_imageViews.resize(handle + 1);
    }
    m_imageViews[handle].view = view;
    return handle;
}

void VulkanRHI::DestroyImageView(ImageViewHandle handle) {
    if (handle != INVALID_HANDLE && handle < m_imageViews.size()) {
        if (m_imageViews[handle].view != VK_NULL_HANDLE) {
            if (_engine && _engine->ctx.device != VK_NULL_HANDLE) {
                vkDestroyImageView(_engine->ctx.device, m_imageViews[handle].view, nullptr);
            }
            m_imageViews[handle].view = VK_NULL_HANDLE;
        }
    }
}



static VkImageLayout MapImageLayout(TextureLayout layout) {
    switch (layout) {
        case TextureLayout::General: return VK_IMAGE_LAYOUT_GENERAL;
        case TextureLayout::ShaderReadOnlyOptimal: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        default: return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

void VulkanRHI::UpdateDescriptorSets(uint32_t writeCount, const WriteDescriptorSet* pDescriptorWrites) {
    VkWriteDescriptorSet* vkWrites = static_cast<VkWriteDescriptorSet*>(arena_alloc(&tls_scratch.arena, writeCount * sizeof(VkWriteDescriptorSet), alignof(VkWriteDescriptorSet)));
    
    uint32_t totalImages = 0;
    uint32_t totalBuffers = 0;
    for (uint32_t i = 0; i < writeCount; ++i) {
        if (pDescriptorWrites[i].pImageInfo) totalImages += pDescriptorWrites[i].descriptorCount;
        if (pDescriptorWrites[i].pBufferInfo) totalBuffers += pDescriptorWrites[i].descriptorCount;
    }
    
    VkDescriptorImageInfo* vkImageInfos = nullptr;
    if (totalImages > 0) {
        vkImageInfos = static_cast<VkDescriptorImageInfo*>(arena_alloc(&tls_scratch.arena, totalImages * sizeof(VkDescriptorImageInfo), alignof(VkDescriptorImageInfo)));
    }
    
    VkDescriptorBufferInfo* vkBufferInfos = nullptr;
    if (totalBuffers > 0) {
        vkBufferInfos = static_cast<VkDescriptorBufferInfo*>(arena_alloc(&tls_scratch.arena, totalBuffers * sizeof(VkDescriptorBufferInfo), alignof(VkDescriptorBufferInfo)));
    }

    uint32_t currentImageIndex = 0;
    uint32_t currentBufferIndex = 0;

    for (uint32_t i = 0; i < writeCount; ++i) {
        vkWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        vkWrites[i].pNext = nullptr;
        vkWrites[i].dstSet = GetVkDescriptorSet(pDescriptorWrites[i].dstSet);
        vkWrites[i].dstBinding = pDescriptorWrites[i].dstBinding;
        vkWrites[i].dstArrayElement = pDescriptorWrites[i].dstArrayElement;
        vkWrites[i].descriptorCount = pDescriptorWrites[i].descriptorCount;
        vkWrites[i].pImageInfo = nullptr;
        vkWrites[i].pBufferInfo = nullptr;
        vkWrites[i].pTexelBufferView = nullptr;
        
        vkWrites[i].descriptorType = MapDescriptorType(pDescriptorWrites[i].descriptorType);

        if (pDescriptorWrites[i].pImageInfo) {
            uint32_t startIndex = currentImageIndex;
            for (uint32_t j = 0; j < pDescriptorWrites[i].descriptorCount; ++j) {
                VkDescriptorImageInfo info{};
                info.sampler = GetVkSampler(pDescriptorWrites[i].pImageInfo[j].sampler);
                if (pDescriptorWrites[i].pImageInfo[j].imageView != INVALID_HANDLE) {
                    info.imageView = GetVkImageViewForHandle(pDescriptorWrites[i].pImageInfo[j].imageView);
                } else {
                    info.imageView = GetVkImageView(pDescriptorWrites[i].pImageInfo[j].texture);
                }
                info.imageLayout = MapImageLayout(pDescriptorWrites[i].pImageInfo[j].imageLayout);
                vkImageInfos[currentImageIndex++] = info;
            }
            vkWrites[i].pImageInfo = &vkImageInfos[startIndex];
        } 
        
        if (pDescriptorWrites[i].pBufferInfo) {
            uint32_t startIndex = currentBufferIndex;
            for (uint32_t j = 0; j < pDescriptorWrites[i].descriptorCount; ++j) {
                VkDescriptorBufferInfo info{};
                info.buffer = GetVkBuffer(pDescriptorWrites[i].pBufferInfo[j].buffer);
                info.offset = pDescriptorWrites[i].pBufferInfo[j].offset;
                info.range = pDescriptorWrites[i].pBufferInfo[j].range;
                vkBufferInfos[currentBufferIndex++] = info;
            }
            vkWrites[i].pBufferInfo = &vkBufferInfos[startIndex];
        }
    }
    vkUpdateDescriptorSets(_engine->ctx.device, writeCount, vkWrites, 0, nullptr);
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

VkDescriptorSetLayout VulkanRHI::GetVkDescriptorSetLayout(DescriptorLayoutHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_descriptorLayouts.size()) return m_descriptorLayouts[handle].layout;
    return VK_NULL_HANDLE;
}

VkDescriptorPool VulkanRHI::GetVkDescriptorPool(DescriptorPoolHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_descriptorPools.size()) return m_descriptorPools[handle].pool;
    return VK_NULL_HANDLE;
}

VkDescriptorSet VulkanRHI::GetVkDescriptorSet(DescriptorSetHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_descriptorSets.size()) return m_descriptorSets[handle].set;
    return VK_NULL_HANDLE;
}

SwapchainStatus VulkanRHI::AcquireNextImage(uint32_t* imageIndex) {
    if (vkWaitForFences(_engine->ctx.device, 1, &_engine->inFlightFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        return SwapchainStatus::Error;
    }
    if (vkResetFences(_engine->ctx.device, 1, &_engine->inFlightFence) != VK_SUCCESS) {
        return SwapchainStatus::Error;
    }

    VkResult acquireResult = vkAcquireNextImageKHR(_engine->ctx.device, _engine->swapchainMgr.swapchain, UINT64_MAX, _engine->imageAvailableSemaphore, nullptr, imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        return SwapchainStatus::NeedRecreate;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        return SwapchainStatus::Error;
    }
    return SwapchainStatus::Ok;
}

void VulkanRHI::UpdateUBO(const UBOData& data) {
    constexpr size_t kUboStride = (sizeof(UBOData) + 255) & ~255;
    uint32_t slot = _engine->currentFrameIndex % 3;
    uint8_t* dst = static_cast<uint8_t*>(_engine->uniformBufferMapped) + (slot * kUboStride);
    memcpy(dst, &data, sizeof(UBOData));
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
    if (vkQueueSubmit(_engine->ctx.graphicsQueue, 1, &si, _engine->inFlightFence) != VK_SUCCESS) {
        return SwapchainStatus::Error;
    }
    VkPresentInfoKHR pri{};
    pri.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pri.waitSemaphoreCount = 1;
    pri.pWaitSemaphores = &_engine->renderFinishedSemaphore;
    pri.swapchainCount = 1;
    pri.pSwapchains = &_engine->swapchainMgr.swapchain;
    pri.pImageIndices = &imageIndex;
    VkResult res = vkQueuePresentKHR(_engine->ctx.presentQueue, &pri);
    _engine->currentFrameIndex++;
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        return SwapchainStatus::NeedRecreate;
    }
    return (res == VK_SUCCESS) ? SwapchainStatus::Ok : SwapchainStatus::Error;
}

RHIResult VulkanRHI::BeginFrame() {
    if (vkResetCommandBuffer(_engine->commandBuffer, 0) != VK_SUCCESS) {
        return RHIResult::ErrorInitializationFailed;
    }
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bool res = (vkBeginCommandBuffer(_engine->commandBuffer, &bi) == VK_SUCCESS);
    if (!res) return RHIResult::ErrorInitializationFailed;
    if (m_mainCmdList == nullptr) {
        m_mainCmdList = new VulkanCommandList(this, _engine->commandBuffer);
    }
    return RHIResult::Success;
}

IRenderCommandList* VulkanRHI::GetMainCommandList() {
    return m_mainCmdList;
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
    rp.framebuffer = _engine->swapchainMgr.colorFramebuffer;
    rp.renderArea.extent = _engine->swapchainMgr.swapchainExtent;
    rp.clearValueCount = 2;
    rp.pClearValues = cl;

    vkCmdBeginRenderPass(_engine->commandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = (float)_engine->swapchainMgr.swapchainExtent.width;
    vp.height = (float)_engine->swapchainMgr.swapchainExtent.height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(_engine->commandBuffer, 0, 1, &vp);

    VkRect2D sc{};
    sc.offset = {0, 0};
    sc.extent = _engine->swapchainMgr.swapchainExtent;
    vkCmdSetScissor(_engine->commandBuffer, 0, 1, &sc);
}

void VulkanRHI::BeginRenderPassLoad() {
    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = _engine->renderPassLoad;
    rp.framebuffer = _engine->swapchainMgr.colorFramebuffer;
    rp.renderArea.extent = _engine->swapchainMgr.swapchainExtent;
    rp.clearValueCount = 0;
    rp.pClearValues = nullptr;

    vkCmdBeginRenderPass(_engine->commandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = (float)_engine->swapchainMgr.swapchainExtent.width;
    vp.height = (float)_engine->swapchainMgr.swapchainExtent.height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(_engine->commandBuffer, 0, 1, &vp);

    VkRect2D sc{};
    sc.offset = {0, 0};
    sc.extent = _engine->swapchainMgr.swapchainExtent;
    vkCmdSetScissor(_engine->commandBuffer, 0, 1, &sc);
}

void VulkanRHI::EndRenderPass() {
    vkCmdEndRenderPass(_engine->commandBuffer);
}

PipelineHandle VulkanRHI::GetPipeline(PipelineType type) const {
    switch (type) {
    case PipelineType::Graphics: return _engine->graphicsPipeline;
    case PipelineType::Billboard: return _engine->billboardPipeline;
    case PipelineType::Wireframe: return _engine->wireframePipeline;
    case PipelineType::DebugLine: return _engine->debugLinePipeline;
    case PipelineType::DebugTriangle: return _engine->debugTrianglePipeline;
    case PipelineType::Skybox: return _engine->skyboxPipeline;
    default: return INVALID_HANDLE;
    }
}

void VulkanRHI::BindGlobalDescriptor(IRenderCommandList* cmdList) {
    constexpr size_t kUboStride = (sizeof(UBOData) + 255) & ~255;
    uint32_t dynamicOffset = static_cast<uint32_t>((_engine->currentFrameIndex % 3) * kUboStride);
    if (_engine->globalBindGroup.is_valid()) {
        cmdList->SetGraphicsBindGroup(0, _engine->globalBindGroup.get(), 1, &dynamicOffset);
    } else {
        DescriptorSetHandle set = _engine->descriptorSet;
        cmdList->BindDescriptorSetsWithDynamicOffsets(_engine->pipelineLayout, 0, 1, &set, 1, &dynamicOffset, false);
    }
}

void VulkanRHI::BindMeshBuffers(IRenderCommandList* cmdList, bool isBillboard) {
    if (!isBillboard) {
        if (_engine->vertexBuffer != INVALID_HANDLE) {
            BufferHandle buffers[] = {_engine->vertexBuffer};
            uint64_t offsets[] = {0};
            cmdList->BindVertexBuffers(0, 1, buffers, offsets);
        }
        if (_engine->indexBuffer != INVALID_HANDLE) {
            cmdList->BindIndexBuffer(_engine->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }
    }
}


void VulkanRHI::PushDebugConstants(const void* data, uint32_t size) {
    vkCmdPushConstants(_engine->commandBuffer, GetVkPipelineLayout(_engine->debugPipelineLayout), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, size, data);
}

void VulkanRHI::UpdateBillboardInstances(const uint32_t* indices, std::size_t count) {
    if (_engine->billboardBuffer != INVALID_HANDLE && _engine->billboardBuffer < m_buffers.size()) {
        void* mapped = m_buffers[_engine->billboardBuffer].mappedData;
        if (mapped) {
            memcpy(mapped, indices, count * sizeof(uint32_t));
        }
    }
}

void VulkanRHI::BeginDebugLabel(const char* name, float r, float g, float b) {
    vk_begin_label(_engine->ctx.device, _engine->commandBuffer, name, r, g, b);
}

void VulkanRHI::EndDebugLabel() {
    vk_end_label(_engine->ctx.device, _engine->commandBuffer);
}

void VulkanRHI::CollectProfiling() {
    tracy_vk_collect(_engine, VK_NULL_HANDLE);
}

void VulkanRHI::GetResolution(uint32_t* width, uint32_t* height) const {
    *width = _engine->swapchainMgr.swapchainExtent.width;
    *height = _engine->swapchainMgr.swapchainExtent.height;
}

void* VulkanRHI::GetOpaqueTracyContext() const {
    return _engine->tracyVkContext;
}

CommandBufferHandle VulkanRHI::GetOpaqueCommandBuffer() const {
    return {_engine->commandBuffer};
}

PipelineLayoutHandle VulkanRHI::CreatePipelineLayout(const PipelineLayoutDesc& desc, const char* name) {
    VkDescriptorSetLayout* vkLayouts = nullptr;
    if (desc.layoutCount > 0) {
        vkLayouts = static_cast<VkDescriptorSetLayout*>(__builtin_alloca(desc.layoutCount * sizeof(VkDescriptorSetLayout)));
        for (uint32_t i = 0; i < desc.layoutCount; ++i) {
            vkLayouts[i] = GetVkDescriptorSetLayout(desc.layouts[i]);
        }
    }

    VkPushConstantRange* vkPushConstants = nullptr;
    if (desc.pushConstantCount > 0) {
        vkPushConstants = static_cast<VkPushConstantRange*>(__builtin_alloca(desc.pushConstantCount * sizeof(VkPushConstantRange)));
        for (uint32_t i = 0; i < desc.pushConstantCount; ++i) {
            vkPushConstants[i].stageFlags = (VkShaderStageFlags)desc.pushConstants[i].stageFlags;
            vkPushConstants[i].offset = desc.pushConstants[i].offset;
            vkPushConstants[i].size = desc.pushConstants[i].size;
        }
    }

    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = (uint32_t)desc.layoutCount;
    info.pSetLayouts = vkLayouts;
    info.pushConstantRangeCount = (uint32_t)desc.pushConstantCount;
    info.pPushConstantRanges = vkPushConstants;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(_engine->ctx.device, &info, nullptr, &layout) != VK_SUCCESS) {
        return INVALID_HANDLE;
    }
    
    if (name) {
        vk_set_object_name(_engine->ctx.device, (uint64_t)layout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, name);
    }

    uint32_t handle = m_pipelineLayouts.size();
    m_pipelineLayouts.push_back(layout);
    return handle;
}

void VulkanRHI::DestroyPipelineLayout(PipelineLayoutHandle handle) {
    if (handle != INVALID_HANDLE && handle < m_pipelineLayouts.size() && m_pipelineLayouts[handle] != VK_NULL_HANDLE) {
        if (_engine && _engine->ctx.device != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(_engine->ctx.device, m_pipelineLayouts[handle], nullptr);
        }
        m_pipelineLayouts[handle] = VK_NULL_HANDLE;
    }
}

PipelineHandle VulkanRHI::CreateComputePipeline(const ComputePipelineDesc& desc) {
    VkShaderModuleCreateInfo modInfo{};
    modInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    modInfo.codeSize = desc.shaderCodeSize;
    modInfo.pCode = (const uint32_t*)desc.shaderCode;
    
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(_engine->ctx.device, &modInfo, nullptr, &module) != VK_SUCCESS) {
        return INVALID_HANDLE;
    }
    if (desc.name) {
        const char* sname = log_format("%s_CS", desc.name);
        vk_set_object_name(_engine->ctx.device, (uint64_t)module, VK_OBJECT_TYPE_SHADER_MODULE, sname);
    }

    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module;
    info.stage.pName = desc.entryPoint ? desc.entryPoint : "main";
    info.layout = GetVkPipelineLayout(desc.layout);

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(_engine->ctx.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(_engine->ctx.device, module, nullptr);
        return INVALID_HANDLE;
    }
    
    vkDestroyShaderModule(_engine->ctx.device, module, nullptr);

    if (desc.name) {
        vk_set_object_name(_engine->ctx.device, (uint64_t)pipeline, VK_OBJECT_TYPE_PIPELINE, desc.name);
    }

    uint32_t handle = m_pipelines.size();
    m_pipelines.push_back(pipeline);
    if (handle >= m_pipelineToLayout.size()) {
        m_pipelineToLayout.resize(handle + 1, VK_NULL_HANDLE);
    }
    m_pipelineToLayout[handle] = GetVkPipelineLayout(desc.layout);
    return handle;
}

static std::vector<uint32_t> load_spirv_file(const char* path) {
    if (!path) return {};
    FILE* f = fopen(path, "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    size_t size = static_cast<size_t>(ftell(f));
    fseek(f, 0, SEEK_SET);
    std::vector<uint32_t> buffer(size / 4);
    if (fread(buffer.data(), 1, size, f) != size) {
        fclose(f);
        return {};
    }
    fclose(f);
    return buffer;
}

PipelineHandle VulkanRHI::CreateComputePipeline(const DeclarativeComputePipelineDesc& desc) {
    std::vector<uint32_t> fileSpv;
    const void* code = desc.shaderCode;
    size_t codeSize = desc.shaderCodeSize;
    if (!code && desc.shaderPath) {
        fileSpv = load_spirv_file(desc.shaderPath);
        code = fileSpv.data();
        codeSize = fileSpv.size() * sizeof(uint32_t);
    }
    if (!code || codeSize == 0) {
        return INVALID_HANDLE;
    }

    std::vector<VkDescriptorSetLayout> setLayouts;
    setLayouts.reserve(desc.bindGroupLayouts.size());
    for (const auto& layoutDesc : desc.bindGroupLayouts) {
        VkDescriptorSetLayout l = m_descriptorCache.GetOrCreateDescriptorSetLayout(_engine->ctx.device, layoutDesc);
        if (l != VK_NULL_HANDLE) {
            setLayouts.push_back(l);
        }
    }

    VkPipelineLayout pipelineLayout = m_descriptorCache.GetOrCreatePipelineLayout(
        _engine->ctx.device, setLayouts, desc.pushConstantsSize, VK_SHADER_STAGE_COMPUTE_BIT);
    if (pipelineLayout == VK_NULL_HANDLE) {
        return INVALID_HANDLE;
    }

    VkShaderModuleCreateInfo modInfo{};
    modInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    modInfo.codeSize = codeSize;
    modInfo.pCode = static_cast<const uint32_t*>(code);

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(_engine->ctx.device, &modInfo, nullptr, &module) != VK_SUCCESS) {
        return INVALID_HANDLE;
    }
    if (desc.debugName) {
        const char* sname = log_format("%s_CS", desc.debugName);
        vk_set_object_name(_engine->ctx.device, (uint64_t)module, VK_OBJECT_TYPE_SHADER_MODULE, sname);
    }

    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module;
    info.stage.pName = desc.entryPoint ? desc.entryPoint : "main";
    info.layout = pipelineLayout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(_engine->ctx.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(_engine->ctx.device, module, nullptr);
        return INVALID_HANDLE;
    }
    vkDestroyShaderModule(_engine->ctx.device, module, nullptr);

    if (desc.debugName) {
        vk_set_object_name(_engine->ctx.device, (uint64_t)pipeline, VK_OBJECT_TYPE_PIPELINE, desc.debugName);
    }

    uint32_t handle = m_pipelines.size();
    m_pipelines.push_back(pipeline);
    if (handle >= m_pipelineToLayout.size()) {
        m_pipelineToLayout.resize(handle + 1, VK_NULL_HANDLE);
    }
    m_pipelineToLayout[handle] = pipelineLayout;
    return handle;
}

VkDescriptorImageInfo VulkanRHI::PrepareImageDescriptor(const BindGroupEntry& entry, VkDescriptorType& outType) const {
    VkDescriptorImageInfo img{};
    img.sampler = VK_NULL_HANDLE;
    img.imageView = VK_NULL_HANDLE;
    img.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (entry.type == BindingType::Sampler) {
        img.sampler = GetVkSampler(entry.sampler);
        outType = VK_DESCRIPTOR_TYPE_SAMPLER;
    } else if (entry.type == BindingType::SampledTexture) {
        img.imageView = entry.imageView != INVALID_HANDLE ? GetVkImageViewForHandle(entry.imageView) : GetVkImageView(entry.texture);
        img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        outType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    } else if (entry.type == BindingType::CombinedImageSampler) {
        img.sampler = entry.sampler != INVALID_HANDLE ? GetVkSampler(entry.sampler) : VK_NULL_HANDLE;
        img.imageView = entry.imageView != INVALID_HANDLE ? GetVkImageViewForHandle(entry.imageView) : GetVkImageView(entry.texture);
        img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        outType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    } else if (entry.type == BindingType::StorageTexture) {
        img.imageView = entry.imageView != INVALID_HANDLE ? GetVkImageViewForHandle(entry.imageView) : GetVkImageView(entry.texture);
        img.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        outType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    } else if (entry.type == BindingType::InputAttachment) {
        img.imageView = entry.imageView != INVALID_HANDLE ? GetVkImageViewForHandle(entry.imageView) : GetVkImageView(entry.texture);
        img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        outType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    }
    return img;
}

VkDescriptorBufferInfo VulkanRHI::PrepareBufferDescriptor(const BindGroupEntry& entry, VkDescriptorType& outType) const {
    VkDescriptorBufferInfo buf{};
    buf.buffer = GetVkBuffer(entry.buffer);
    buf.offset = entry.offset;
    buf.range = entry.size > 0 ? entry.size : VK_WHOLE_SIZE;
    if (entry.type == BindingType::UniformBuffer) {
        outType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    } else if (entry.type == BindingType::UniformBufferDynamic) {
        outType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    } else if (entry.type == BindingType::StorageBufferDynamic) {
        outType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    } else {
        outType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    return buf;
}

BindGroupHandle VulkanRHI::CreateBindGroup(const BindGroupDesc& desc) {
    VkDescriptorSetLayout layout = m_descriptorCache.GetOrCreateDescriptorSetLayout(_engine->ctx.device, desc.layout);
    if (layout == VK_NULL_HANDLE) {
        return INVALID_HANDLE;
    }

    VkDescriptorSet set = m_descriptorAllocator.Allocate(_engine->ctx.device, layout);
    if (set == VK_NULL_HANDLE) {
        return INVALID_HANDLE;
    }

    if (desc.debugName) {
        vk_set_object_name(_engine->ctx.device, (uint64_t)set, VK_OBJECT_TYPE_DESCRIPTOR_SET, desc.debugName);
    }

    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    writes.reserve(desc.entries.size());
    imageInfos.reserve(desc.entries.size());
    bufferInfos.reserve(desc.entries.size());

    for (const auto& entry : desc.entries) {
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = set;
        w.dstBinding = entry.binding;
        w.dstArrayElement = 0;
        w.descriptorCount = 1;

        if (entry.type == BindingType::UniformBuffer || entry.type == BindingType::UniformBufferDynamic ||
            entry.type == BindingType::StorageBuffer || entry.type == BindingType::StorageBufferDynamic) {
            bufferInfos.push_back(PrepareBufferDescriptor(entry, w.descriptorType));
            w.pBufferInfo = &bufferInfos.back();
        } else {
            imageInfos.push_back(PrepareImageDescriptor(entry, w.descriptorType));
            w.pImageInfo = &imageInfos.back();
        }
        writes.push_back(w);
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(_engine->ctx.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    uint32_t handle = m_nextBindGroupHandle++;
    if (handle >= m_bindGroups.size()) {
        m_bindGroups.resize(handle + 1, VK_NULL_HANDLE);
    }
    m_bindGroups[handle] = set;
    return handle;
}

void VulkanRHI::DestroyBindGroup(BindGroupHandle handle) {
    if (handle != INVALID_HANDLE && handle < m_bindGroups.size()) {
        m_bindGroups[handle] = VK_NULL_HANDLE;
    }
}

VkDescriptorSet VulkanRHI::GetVkBindGroup(BindGroupHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_bindGroups.size()) {
        return m_bindGroups[handle];
    }
    return VK_NULL_HANDLE;
}

VkPipelineLayout VulkanRHI::GetPipelineLayoutForPipeline(PipelineHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_pipelineToLayout.size()) {
        return m_pipelineToLayout[handle];
    }
    return VK_NULL_HANDLE;
}

void VulkanRHI::DestroyPipeline(PipelineHandle handle) {
    if (handle != INVALID_HANDLE && handle < m_pipelines.size() && m_pipelines[handle] != VK_NULL_HANDLE) {
        if (_engine && _engine->ctx.device != VK_NULL_HANDLE) {
            vkDestroyPipeline(_engine->ctx.device, m_pipelines[handle], nullptr);
        }
        m_pipelines[handle] = VK_NULL_HANDLE;
    }
}

VkPipelineLayout VulkanRHI::GetVkPipelineLayout(PipelineLayoutHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_pipelineLayouts.size()) return m_pipelineLayouts[handle];
    return VK_NULL_HANDLE;
}

VkPipeline VulkanRHI::GetVkPipeline(PipelineHandle handle) const {
    if (handle != INVALID_HANDLE && handle < m_pipelines.size()) return m_pipelines[handle];
    return VK_NULL_HANDLE;
}

static VkFormat translate_vertex_format(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float1: return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::Int1: return VK_FORMAT_R32_SINT;
    case VertexFormat::UInt1: return VK_FORMAT_R32_UINT;
    default: return VK_FORMAT_UNDEFINED;
    }
}

static VkCullModeFlags translate_cull_mode(CullMode mode) {
    switch (mode) {
    case CullMode::None: return VK_CULL_MODE_NONE;
    case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
    case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
    }
    return VK_CULL_MODE_NONE;
}

static VkCompareOp translate_compare_op(CompareOp op) {
    switch (op) {
    case CompareOp::Never: return VK_COMPARE_OP_NEVER;
    case CompareOp::Less: return VK_COMPARE_OP_LESS;
    case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_LESS_OR_EQUAL;
}

static VkPrimitiveTopology translate_topology(Topology top) {
    switch (top) {
    case Topology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case Topology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case Topology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

static bool resolve_shader_code(const void* inCode, size_t inSize, const char* path, std::vector<uint32_t>& outStorage, const void*& outCode, size_t& outSize) {
    if (inCode && inSize > 0) {
        outCode = inCode;
        outSize = inSize;
        return true;
    }
    if (path) {
        outStorage = load_spirv_file(path);
        if (!outStorage.empty()) {
            outCode = outStorage.data();
            outSize = outStorage.size() * sizeof(uint32_t);
            return true;
        }
    }
    return false;
}

struct CommonGraphicsConfig {
    VkPipelineLayout layout;
    VkRenderPass renderPass;
    uint32_t subpass;
    const char* debugName;
    Topology topology;
    PolygonMode polygonMode;
    CullMode cullMode;
    FrontFace frontFace;
    bool depthTestEnable;
    bool depthWriteEnable;
    CompareOp depthCompareOp;
    bool colorBlendEnable;
    uint32_t vertexBindingCount;
    const VertexInputBinding* vertexBindings;
    uint32_t vertexAttributeCount;
    const VertexInputAttribute* vertexAttributes;
};

static VkPipeline create_vk_graphics_pipeline_internal(VkDevice device, VkShaderModule vsm, VkShaderModule fsm, const CommonGraphicsConfig& cfg) {
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vsm;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fsm;
    stages[1].pName = "main";

    VkVertexInputBindingDescription* bindings = nullptr;
    if (cfg.vertexBindingCount > 0 && cfg.vertexBindings) {
        bindings = static_cast<VkVertexInputBindingDescription*>(__builtin_alloca(cfg.vertexBindingCount * sizeof(VkVertexInputBindingDescription)));
        for (uint32_t i = 0; i < cfg.vertexBindingCount; ++i) {
            bindings[i].binding = cfg.vertexBindings[i].binding;
            bindings[i].stride = cfg.vertexBindings[i].stride;
            bindings[i].inputRate = cfg.vertexBindings[i].isInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        }
    }

    VkVertexInputAttributeDescription* attrs = nullptr;
    if (cfg.vertexAttributeCount > 0 && cfg.vertexAttributes) {
        attrs = static_cast<VkVertexInputAttributeDescription*>(__builtin_alloca(cfg.vertexAttributeCount * sizeof(VkVertexInputAttributeDescription)));
        for (uint32_t i = 0; i < cfg.vertexAttributeCount; ++i) {
            attrs[i].location = cfg.vertexAttributes[i].location;
            attrs[i].binding = cfg.vertexAttributes[i].binding;
            attrs[i].offset = cfg.vertexAttributes[i].offset;
            attrs[i].format = translate_vertex_format(cfg.vertexAttributes[i].format);
        }
    }

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = cfg.vertexBindingCount;
    vi.pVertexBindingDescriptions = bindings;
    vi.vertexAttributeDescriptionCount = cfg.vertexAttributeCount;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = translate_topology(cfg.topology);

    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = cfg.polygonMode == PolygonMode::Fill ? VK_POLYGON_MODE_FILL : VK_POLYGON_MODE_LINE;
    rs.cullMode = translate_cull_mode(cfg.cullMode);
    rs.frontFace = cfg.frontFace == FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (cfg.colorBlendEnable) {
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
    } else {
        cba.blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = cfg.depthTestEnable ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = cfg.depthWriteEnable ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = translate_compare_op(cfg.depthCompareOp);

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeInfo.stageCount = 2;
    pipeInfo.pStages = stages;
    pipeInfo.pVertexInputState = &vi;
    pipeInfo.pInputAssemblyState = &ia;
    pipeInfo.pViewportState = &vps;
    pipeInfo.pRasterizationState = &rs;
    pipeInfo.pMultisampleState = &ms;
    pipeInfo.pColorBlendState = &cb;
    pipeInfo.pDepthStencilState = &ds;
    pipeInfo.pDynamicState = &dyn;
    pipeInfo.layout = cfg.layout;
    pipeInfo.renderPass = cfg.renderPass;
    pipeInfo.subpass = cfg.subpass;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipeline) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    if (cfg.debugName) {
        vk_set_object_name(device, (uint64_t)pipeline, VK_OBJECT_TYPE_PIPELINE, cfg.debugName);
    }
    return pipeline;
}

PipelineHandle VulkanRHI::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    VkShaderModuleCreateInfo vInfo{};
    vInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vInfo.codeSize = desc.vertexShaderSize;
    vInfo.pCode = static_cast<const uint32_t*>(desc.vertexShaderCode);
    VkShaderModule vsm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(_engine->ctx.device, &vInfo, nullptr, &vsm) != VK_SUCCESS) {
        return INVALID_HANDLE;
    }

    VkShaderModuleCreateInfo fInfo{};
    fInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fInfo.codeSize = desc.fragmentShaderSize;
    fInfo.pCode = static_cast<const uint32_t*>(desc.fragmentShaderCode);
    VkShaderModule fsm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(_engine->ctx.device, &fInfo, nullptr, &fsm) != VK_SUCCESS) {
        vkDestroyShaderModule(_engine->ctx.device, vsm, nullptr);
        return INVALID_HANDLE;
    }
    if (desc.debugName) {
        const char* vname = log_format("%s_VS", desc.debugName);
        const char* fname = log_format("%s_FS", desc.debugName);
        vk_set_object_name(_engine->ctx.device, (uint64_t)vsm, VK_OBJECT_TYPE_SHADER_MODULE, vname);
        vk_set_object_name(_engine->ctx.device, (uint64_t)fsm, VK_OBJECT_TYPE_SHADER_MODULE, fname);
    }

    CommonGraphicsConfig cfg{};
    cfg.layout = GetVkPipelineLayout(desc.layout);
    cfg.renderPass = static_cast<VkRenderPass>(desc.renderPass);
    cfg.subpass = desc.subpass;
    cfg.debugName = desc.debugName;
    cfg.topology = desc.topology;
    cfg.polygonMode = desc.polygonMode;
    cfg.cullMode = desc.cullMode;
    cfg.frontFace = desc.frontFace;
    cfg.depthTestEnable = desc.depthTestEnable;
    cfg.depthWriteEnable = desc.depthWriteEnable;
    cfg.depthCompareOp = desc.depthCompareOp;
    cfg.colorBlendEnable = desc.colorBlendEnable;
    cfg.vertexBindingCount = desc.vertexBindingCount;
    cfg.vertexBindings = desc.vertexBindings;
    cfg.vertexAttributeCount = desc.vertexAttributeCount;
    cfg.vertexAttributes = desc.vertexAttributes;

    VkPipeline pipeline = create_vk_graphics_pipeline_internal(_engine->ctx.device, vsm, fsm, cfg);
    vkDestroyShaderModule(_engine->ctx.device, vsm, nullptr);
    vkDestroyShaderModule(_engine->ctx.device, fsm, nullptr);

    if (pipeline == VK_NULL_HANDLE) {
        return INVALID_HANDLE;
    }

    uint32_t handle = m_pipelines.size();
    m_pipelines.push_back(pipeline);
    return handle;
}

PipelineHandle VulkanRHI::CreateGraphicsPipeline(const DeclarativeGraphicsPipelineDesc& desc) {
    std::vector<uint32_t> vSpv;
    const void* vCode = nullptr;
    size_t vCodeSize = 0;
    if (!resolve_shader_code(desc.vertexShaderCode, desc.vertexShaderSize, desc.vertexShaderPath, vSpv, vCode, vCodeSize)) {
        LOG_ERROR("rhi", "Missing vertex shader for graphics pipeline %s", desc.debugName ? desc.debugName : "Unknown");
        return INVALID_HANDLE;
    }

    std::vector<uint32_t> fSpv;
    const void* fCode = nullptr;
    size_t fCodeSize = 0;
    if (!resolve_shader_code(desc.fragmentShaderCode, desc.fragmentShaderSize, desc.fragmentShaderPath, fSpv, fCode, fCodeSize)) {
        LOG_ERROR("rhi", "Missing fragment shader for graphics pipeline %s", desc.debugName ? desc.debugName : "Unknown");
        return INVALID_HANDLE;
    }

    std::vector<VkDescriptorSetLayout> setLayouts;
    setLayouts.reserve(desc.bindGroupLayouts.size());
    for (const auto& layoutDesc : desc.bindGroupLayouts) {
        VkDescriptorSetLayout l = m_descriptorCache.GetOrCreateDescriptorSetLayout(_engine->ctx.device, layoutDesc);
        if (l != VK_NULL_HANDLE) {
            setLayouts.push_back(l);
        }
    }

    VkShaderStageFlags pushStages = desc.pushConstantStages != 0 ? desc.pushConstantStages : (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    VkPipelineLayout pipelineLayout = m_descriptorCache.GetOrCreatePipelineLayout(
        _engine->ctx.device, setLayouts, desc.pushConstantsSize, pushStages);
    if (pipelineLayout == VK_NULL_HANDLE) {
        LOG_ERROR("rhi", "Failed to create pipeline layout for %s", desc.debugName ? desc.debugName : "Unknown");
        return INVALID_HANDLE;
    }

    VkShaderModuleCreateInfo vInfo{};
    vInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vInfo.codeSize = vCodeSize;
    vInfo.pCode = static_cast<const uint32_t*>(vCode);
    VkShaderModule vsm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(_engine->ctx.device, &vInfo, nullptr, &vsm) != VK_SUCCESS) {
        return INVALID_HANDLE;
    }

    VkShaderModuleCreateInfo fInfo{};
    fInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fInfo.codeSize = fCodeSize;
    fInfo.pCode = static_cast<const uint32_t*>(fCode);
    VkShaderModule fsm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(_engine->ctx.device, &fInfo, nullptr, &fsm) != VK_SUCCESS) {
        vkDestroyShaderModule(_engine->ctx.device, vsm, nullptr);
        return INVALID_HANDLE;
    }

    if (desc.debugName) {
        const char* vname = log_format("%s_VS", desc.debugName);
        const char* fname = log_format("%s_FS", desc.debugName);
        vk_set_object_name(_engine->ctx.device, (uint64_t)vsm, VK_OBJECT_TYPE_SHADER_MODULE, vname);
        vk_set_object_name(_engine->ctx.device, (uint64_t)fsm, VK_OBJECT_TYPE_SHADER_MODULE, fname);
    }

    CommonGraphicsConfig cfg{};
    cfg.layout = pipelineLayout;
    cfg.renderPass = static_cast<VkRenderPass>(desc.renderPass);
    cfg.subpass = desc.subpass;
    cfg.debugName = desc.debugName;
    cfg.topology = desc.topology;
    cfg.polygonMode = desc.polygonMode;
    cfg.cullMode = desc.cullMode;
    cfg.frontFace = desc.frontFace;
    cfg.depthTestEnable = desc.depthTestEnable;
    cfg.depthWriteEnable = desc.depthWriteEnable;
    cfg.depthCompareOp = desc.depthCompareOp;
    cfg.colorBlendEnable = desc.colorBlendEnable;
    cfg.vertexBindingCount = static_cast<uint32_t>(desc.vertexBindings.size());
    cfg.vertexBindings = desc.vertexBindings.data();
    cfg.vertexAttributeCount = static_cast<uint32_t>(desc.vertexAttributes.size());
    cfg.vertexAttributes = desc.vertexAttributes.data();

    VkPipeline pipeline = create_vk_graphics_pipeline_internal(_engine->ctx.device, vsm, fsm, cfg);
    vkDestroyShaderModule(_engine->ctx.device, vsm, nullptr);
    vkDestroyShaderModule(_engine->ctx.device, fsm, nullptr);

    if (pipeline == VK_NULL_HANDLE) {
        return INVALID_HANDLE;
    }

    uint32_t handle = m_pipelines.size();
    m_pipelines.push_back(pipeline);
    if (handle >= m_pipelineToLayout.size()) {
        m_pipelineToLayout.resize(handle + 1, VK_NULL_HANDLE);
    }
    m_pipelineToLayout[handle] = pipelineLayout;
    return handle;
}



void VulkanRHI::CmdPipelineBarrier(VkCommandBuffer cb, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) {
    (void)this;
    vkCmdPipelineBarrier(cb, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}
void VulkanRHI::CmdCopyImageToBuffer(VkCommandBuffer cb, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions) {
    (void)this;
    vkCmdCopyImageToBuffer(cb, srcImage, srcImageLayout, dstBuffer, regionCount, pRegions);
}
void VulkanRHI::CmdCopyBufferToImage(VkCommandBuffer cb, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) {
    (void)this;
    vkCmdCopyBufferToImage(cb, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
}
void VulkanRHI::CmdBlitImage(VkCommandBuffer cb, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter) {
    (void)this;
    vkCmdBlitImage(cb, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, filter);
}

