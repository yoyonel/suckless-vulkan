#include "vulkan_rhi.h"
#include <string>
#include "../vk_engine.h"
#include "../tracy_vulkan.h"
#include <cstring>

#include "vk_engine.h"
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
        VulkanRHI* vkRhi = static_cast<VulkanRHI*>(rhi);
        VulkanEngine* engine = vkRhi->_engine;
        vkRhi->~VulkanRHI();
        engine->~VulkanEngine();
        // Memory is explicitly left in the arena; reclaimed on next CreateRHI or shutdown.
    }
}

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
    return init_vulkan_engine(_engine);
}

void VulkanRHI::Shutdown() {
    cleanup_vulkan_engine(_engine);
}

#include "vk_engine_runtime.h"

bool VulkanRHI::DrawFrame() {
    return draw_frame(_engine);
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
    tex.format = ii.format;
    
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
    std::vector<VkDescriptorSetLayoutBinding> vkBindings(desc.bindingCount);
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
    info.pBindings = vkBindings.data();

    VulkanDescriptorLayout layout;
    if (vkCreateDescriptorSetLayout(_engine->device, &info, nullptr, &layout.layout) != VK_SUCCESS) {
        return INVALID_HANDLE;
    }

    if (name) vk_set_object_name(_engine->device, (uint64_t)layout.layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, name);

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
            vkDestroyDescriptorSetLayout(_engine->device, layout.layout, nullptr);
            layout.layout = VK_NULL_HANDLE;
        }
    }
}

DescriptorPoolHandle VulkanRHI::CreateDescriptorPool(const DescriptorPoolDesc& desc, const char* name) {
    std::vector<VkDescriptorPoolSize> vkPoolSizes(desc.poolSizeCount);
    for (uint32_t i = 0; i < desc.poolSizeCount; ++i) {
        vkPoolSizes[i].type = MapDescriptorType(desc.poolSizes[i].type);
        vkPoolSizes[i].descriptorCount = desc.poolSizes[i].descriptorCount;
    }

    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.poolSizeCount = desc.poolSizeCount;
    info.pPoolSizes = vkPoolSizes.data();
    info.maxSets = desc.maxSets;

    VulkanDescriptorPool pool;
    if (vkCreateDescriptorPool(_engine->device, &info, nullptr, &pool.pool) != VK_SUCCESS) {
        return INVALID_HANDLE;
    }

    if (name) vk_set_object_name(_engine->device, (uint64_t)pool.pool, VK_OBJECT_TYPE_DESCRIPTOR_POOL, name);

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
            vkDestroyDescriptorPool(_engine->device, pool.pool, nullptr);
            pool.pool = VK_NULL_HANDLE;
        }
    }
}

bool VulkanRHI::AllocateDescriptorSets(const DescriptorSetAllocateDesc& desc, DescriptorSetHandle* outSets) {
    if (desc.pool == INVALID_HANDLE || desc.pool >= m_descriptorPools.size()) return false;
    
    std::vector<VkDescriptorSetLayout> vkLayouts(desc.setCount);
    for (uint32_t i = 0; i < desc.setCount; ++i) {
        if (desc.layouts[i] == INVALID_HANDLE || desc.layouts[i] >= m_descriptorLayouts.size()) return false;
        vkLayouts[i] = m_descriptorLayouts[desc.layouts[i]].layout;
    }

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_descriptorPools[desc.pool].pool;
    ai.descriptorSetCount = desc.setCount;
    ai.pSetLayouts = vkLayouts.data();

    std::vector<VkDescriptorSet> vkSets(desc.setCount);
    if (vkAllocateDescriptorSets(_engine->device, &ai, vkSets.data()) != VK_SUCCESS) {
        return false;
    }

    for (uint32_t i = 0; i < desc.setCount; ++i) {
        uint32_t handle = m_nextDescriptorSetHandle++;
        if (handle >= m_descriptorSets.size()) {
            m_descriptorSets.resize(handle + 1);
        }
        m_descriptorSets[handle] = {vkSets[i]};
        outSets[i] = handle;
    }
    return true;
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
    if (vi.format == _engine->depthFormat) {
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
    if (vkCreateImageView(_engine->device, &vi, nullptr, &view) != VK_SUCCESS) {
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
            vkDestroyImageView(_engine->device, m_imageViews[handle].view, nullptr);
            m_imageViews[handle].view = VK_NULL_HANDLE;
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void VulkanRHI::UpdateDescriptorSets(uint32_t writeCount, const WriteDescriptorSet* pDescriptorWrites) {
    VkWriteDescriptorSet* vkWrites = static_cast<VkWriteDescriptorSet*>(__builtin_alloca(writeCount * sizeof(VkWriteDescriptorSet)));
    
    uint32_t totalImages = 0;
    uint32_t totalBuffers = 0;
    for (uint32_t i = 0; i < writeCount; ++i) {
        if (pDescriptorWrites[i].pImageInfo) totalImages += pDescriptorWrites[i].descriptorCount;
        if (pDescriptorWrites[i].pBufferInfo) totalBuffers += pDescriptorWrites[i].descriptorCount;
    }
    
    VkDescriptorImageInfo* vkImageInfos = nullptr;
    if (totalImages > 0) {
        vkImageInfos = static_cast<VkDescriptorImageInfo*>(__builtin_alloca(totalImages * sizeof(VkDescriptorImageInfo)));
    }
    
    VkDescriptorBufferInfo* vkBufferInfos = nullptr;
    if (totalBuffers > 0) {
        vkBufferInfos = static_cast<VkDescriptorBufferInfo*>(__builtin_alloca(totalBuffers * sizeof(VkDescriptorBufferInfo)));
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
        
        switch (pDescriptorWrites[i].descriptorType) {
            case DescriptorType::UniformBuffer: vkWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; break;
            case DescriptorType::StorageBuffer: vkWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; break;
            case DescriptorType::CombinedImageSampler: vkWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; break;
            case DescriptorType::StorageImage: vkWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; break;
            default: break;
        }

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
                switch (pDescriptorWrites[i].pImageInfo[j].imageLayout) {
                    case TextureLayout::General: info.imageLayout = VK_IMAGE_LAYOUT_GENERAL; break;
                    case TextureLayout::ShaderReadOnlyOptimal: info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; break;
                    default: info.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED; break;
                }
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
    vkUpdateDescriptorSets(_engine->device, writeCount, vkWrites, 0, nullptr);
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

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = (float)_engine->swapchainExtent.width;
    vp.height = (float)_engine->swapchainExtent.height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(_engine->commandBuffer, 0, 1, &vp);

    VkRect2D sc{};
    sc.offset = {0, 0};
    sc.extent = _engine->swapchainExtent;
    vkCmdSetScissor(_engine->commandBuffer, 0, 1, &sc);
}

void VulkanRHI::EndRenderPass() {
    vkCmdEndRenderPass(_engine->commandBuffer);
}

void VulkanRHI::BindPipeline(PipelineType type) {
    PipelineHandle pipeline = INVALID_HANDLE;
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
    if (pipeline != INVALID_HANDLE) {
        vkCmdBindPipeline(_engine->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GetVkPipeline(pipeline));
    }
}

void VulkanRHI::BindGlobalDescriptor() {
    VkDescriptorSet vkSet = GetVkDescriptorSet(_engine->descriptorSet);
    vkCmdBindDescriptorSets(_engine->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GetVkPipelineLayout(_engine->pipelineLayout), 0, 1, &vkSet, 0, nullptr);
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
    vkCmdPushConstants(_engine->commandBuffer, GetVkPipelineLayout(_engine->debugPipelineLayout), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, size, data);
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

PipelineLayoutHandle VulkanRHI::CreatePipelineLayout(const PipelineLayoutDesc& desc, const char* name) {
    std::vector<VkDescriptorSetLayout> vkLayouts;
    vkLayouts.reserve(desc.layoutCount);
    for (uint32_t i = 0; i < desc.layoutCount; ++i) {
        vkLayouts.push_back(GetVkDescriptorSetLayout(desc.layouts[i]));
    }

    std::vector<VkPushConstantRange> vkPushConstants;
    vkPushConstants.reserve(desc.pushConstantCount);
    for (uint32_t i = 0; i < desc.pushConstantCount; ++i) {
        VkPushConstantRange pc{};
        pc.stageFlags = (VkShaderStageFlags)desc.pushConstants[i].stageFlags; // cast matches
        pc.offset = desc.pushConstants[i].offset;
        pc.size = desc.pushConstants[i].size;
        vkPushConstants.push_back(pc);
    }

    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = (uint32_t)vkLayouts.size();
    info.pSetLayouts = vkLayouts.data();
    info.pushConstantRangeCount = (uint32_t)vkPushConstants.size();
    info.pPushConstantRanges = vkPushConstants.data();

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(_engine->device, &info, nullptr, &layout) != VK_SUCCESS) {
        return INVALID_HANDLE;
    }
    
    if (name) {
        vk_set_object_name(_engine->device, (uint64_t)layout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, name);
    }

    uint32_t handle = m_pipelineLayouts.size();
    m_pipelineLayouts.push_back(layout);
    return handle;
}

void VulkanRHI::DestroyPipelineLayout(PipelineLayoutHandle handle) {
    if (handle != INVALID_HANDLE && handle < m_pipelineLayouts.size() && m_pipelineLayouts[handle] != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(_engine->device, m_pipelineLayouts[handle], nullptr);
        m_pipelineLayouts[handle] = VK_NULL_HANDLE;
    }
}

PipelineHandle VulkanRHI::CreateComputePipeline(const ComputePipelineDesc& desc) {
    VkShaderModuleCreateInfo modInfo{};
    modInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    modInfo.codeSize = desc.shaderCodeSize;
    modInfo.pCode = (const uint32_t*)desc.shaderCode;
    
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(_engine->device, &modInfo, nullptr, &module) != VK_SUCCESS) {
        return INVALID_HANDLE;
    }

    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module;
    info.stage.pName = desc.entryPoint ? desc.entryPoint : "main";
    info.layout = GetVkPipelineLayout(desc.layout);

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(_engine->device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(_engine->device, module, nullptr);
        return INVALID_HANDLE;
    }
    
    vkDestroyShaderModule(_engine->device, module, nullptr);

    if (desc.name) {
        vk_set_object_name(_engine->device, (uint64_t)pipeline, VK_OBJECT_TYPE_PIPELINE, desc.name);
    }

    uint32_t handle = m_pipelines.size();
    m_pipelines.push_back(pipeline);
    return handle;
}

void VulkanRHI::DestroyPipeline(PipelineHandle handle) {
    if (handle != INVALID_HANDLE && handle < m_pipelines.size() && m_pipelines[handle] != VK_NULL_HANDLE) {
        vkDestroyPipeline(_engine->device, m_pipelines[handle], nullptr);
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


PipelineHandle VulkanRHI::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    VkShaderModuleCreateInfo vInfo{};
    vInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vInfo.codeSize = desc.vertexShaderSize;
    vInfo.pCode = (const uint32_t*)desc.vertexShaderCode;
    VkShaderModule vsm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(_engine->device, &vInfo, nullptr, &vsm) != VK_SUCCESS) {
        return INVALID_HANDLE;
    }

    VkShaderModuleCreateInfo fInfo{};
    fInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fInfo.codeSize = desc.fragmentShaderSize;
    fInfo.pCode = (const uint32_t*)desc.fragmentShaderCode;
    VkShaderModule fsm = VK_NULL_HANDLE;
    if (vkCreateShaderModule(_engine->device, &fInfo, nullptr, &fsm) != VK_SUCCESS) {
        vkDestroyShaderModule(_engine->device, vsm, nullptr);
        return INVALID_HANDLE;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vsm;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fsm;
    stages[1].pName = "main";

    std::vector<VkVertexInputBindingDescription> bindings(desc.vertexBindingCount);
    for(uint32_t i=0; i<desc.vertexBindingCount; ++i) {
        bindings[i].binding = desc.vertexBindings[i].binding;
        bindings[i].stride = desc.vertexBindings[i].stride;
        bindings[i].inputRate = desc.vertexBindings[i].isInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
    }

    std::vector<VkVertexInputAttributeDescription> attrs(desc.vertexAttributeCount);
    for(uint32_t i=0; i<desc.vertexAttributeCount; ++i) {
        attrs[i].location = desc.vertexAttributes[i].location;
        attrs[i].binding = desc.vertexAttributes[i].binding;
        attrs[i].offset = desc.vertexAttributes[i].offset;
        switch(desc.vertexAttributes[i].format) {
            case VertexFormat::Float1: attrs[i].format = VK_FORMAT_R32_SFLOAT; break;
            case VertexFormat::Float2: attrs[i].format = VK_FORMAT_R32G32_SFLOAT; break;
            case VertexFormat::Float3: attrs[i].format = VK_FORMAT_R32G32B32_SFLOAT; break;
            case VertexFormat::Float4: attrs[i].format = VK_FORMAT_R32G32B32A32_SFLOAT; break;
            case VertexFormat::Int1: attrs[i].format = VK_FORMAT_R32_SINT; break;
            default: attrs[i].format = VK_FORMAT_R32G32B32_SFLOAT; break;
        }
    }

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = (uint32_t)bindings.size();
    vi.pVertexBindingDescriptions = bindings.data();
    vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
    vi.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    switch(desc.topology) {
        case Topology::TriangleList: ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
        case Topology::LineList: ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
        case Topology::PointList: ia.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
    }
    
    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = desc.polygonMode == PolygonMode::Fill ? VK_POLYGON_MODE_FILL : VK_POLYGON_MODE_LINE;
    switch(desc.cullMode) {
        case CullMode::None: rs.cullMode = VK_CULL_MODE_NONE; break;
        case CullMode::Front: rs.cullMode = VK_CULL_MODE_FRONT_BIT; break;
        case CullMode::Back: rs.cullMode = VK_CULL_MODE_BACK_BIT; break;
        case CullMode::FrontAndBack: rs.cullMode = VK_CULL_MODE_FRONT_AND_BACK; break;
    }
    rs.frontFace = desc.frontFace == FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (desc.colorBlendEnable) {
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
    ds.depthTestEnable = desc.depthTestEnable ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = desc.depthWriteEnable ? VK_TRUE : VK_FALSE;
    switch(desc.depthCompareOp) {
        case CompareOp::Never: ds.depthCompareOp = VK_COMPARE_OP_NEVER; break;
        case CompareOp::Less: ds.depthCompareOp = VK_COMPARE_OP_LESS; break;
        case CompareOp::Equal: ds.depthCompareOp = VK_COMPARE_OP_EQUAL; break;
        case CompareOp::LessOrEqual: ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; break;
        case CompareOp::Greater: ds.depthCompareOp = VK_COMPARE_OP_GREATER; break;
        case CompareOp::NotEqual: ds.depthCompareOp = VK_COMPARE_OP_NOT_EQUAL; break;
        case CompareOp::GreaterOrEqual: ds.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; break;
        case CompareOp::Always: ds.depthCompareOp = VK_COMPARE_OP_ALWAYS; break;
    }

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
    pipeInfo.layout = GetVkPipelineLayout(desc.layout);
    pipeInfo.renderPass = (VkRenderPass)desc.renderPass;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(_engine->device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(_engine->device, vsm, nullptr);
        vkDestroyShaderModule(_engine->device, fsm, nullptr);
        return INVALID_HANDLE;
    }

    if (desc.debugName) {
        vk_set_object_name(_engine->device, (uint64_t)pipeline, VK_OBJECT_TYPE_PIPELINE, desc.debugName);
    }

    vkDestroyShaderModule(_engine->device, vsm, nullptr);
    vkDestroyShaderModule(_engine->device, fsm, nullptr);

    uint32_t handle = m_pipelines.size();
    m_pipelines.push_back(pipeline);
    return handle;
}

void VulkanRHI::CmdBindPipeline(CommandBufferHandle cb, PipelineHandle pipeline, bool isCompute) {
    vkCmdBindPipeline((VkCommandBuffer)cb, isCompute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS, GetVkPipeline(pipeline));
}
void VulkanRHI::CmdBindDescriptorSets(CommandBufferHandle cb, PipelineLayoutHandle layout, uint32_t firstSet, uint32_t count, const DescriptorSetHandle* sets, bool isCompute) {
    if (count == 0 || sets == nullptr) return;
    VkDescriptorSet* vkSets = static_cast<VkDescriptorSet*>(__builtin_alloca(count * sizeof(VkDescriptorSet)));
    for(uint32_t i=0; i<count; ++i) vkSets[i] = GetVkDescriptorSet(sets[i]);
    vkCmdBindDescriptorSets((VkCommandBuffer)cb, isCompute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS, GetVkPipelineLayout(layout), firstSet, count, vkSets, 0, nullptr);
}
void VulkanRHI::CmdPushConstants(CommandBufferHandle cb, PipelineLayoutHandle layout, ShaderStage stage, uint32_t offset, uint32_t size, const void* values) {
    VkShaderStageFlags flags = 0;
    if ((uint32_t)stage & (uint32_t)ShaderStage::Vertex) flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if ((uint32_t)stage & (uint32_t)ShaderStage::Fragment) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if ((uint32_t)stage & (uint32_t)ShaderStage::Compute) flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    vkCmdPushConstants((VkCommandBuffer)cb, GetVkPipelineLayout(layout), flags, offset, size, values);
}
void VulkanRHI::CmdDraw(CommandBufferHandle cb, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    vkCmdDraw((VkCommandBuffer)cb, vertexCount, instanceCount, firstVertex, firstInstance);
}
void VulkanRHI::CmdDrawIndexed(CommandBufferHandle cb, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    vkCmdDrawIndexed((VkCommandBuffer)cb, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanRHI::CmdDispatch(CommandBufferHandle cb, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    vkCmdDispatch((VkCommandBuffer)cb, groupCountX, groupCountY, groupCountZ);
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
