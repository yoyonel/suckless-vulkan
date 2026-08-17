#include "vk_engine_ibl.h"
#include "app_log.h"
#include "rhi/vulkan_command_list.h"
#include "rhi/vulkan_rhi.h"
#include "tracy_client.h"
#include "tracy_state.h"
#include "tracy_vulkan.h"
#include "vk_engine.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stb/stb_image_write.h>
#include <sys/stat.h>
#include <vector>

namespace {

VkCommandBuffer begin_single_time_commands(VulkanEngine* engine) {
    if (!engine || engine->ctx.commandPool == VK_NULL_HANDLE || engine->ctx.device == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = engine->ctx.commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(engine->ctx.device, &allocInfo, &cb) != VK_SUCCESS) {
        LOG_ERROR("ibl", "Failed to allocate command buffer");
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cb, &beginInfo) != VK_SUCCESS) {
        LOG_ERROR("ibl", "Failed to begin command buffer");
        vkFreeCommandBuffers(engine->ctx.device, engine->ctx.commandPool, 1, &cb);
        return VK_NULL_HANDLE;
    }

    return cb;
}

void end_single_time_commands(VulkanEngine* engine, VkCommandBuffer cb, VkFence fence = VK_NULL_HANDLE) {
    if (!engine || cb == VK_NULL_HANDLE)
        return;

    vkEndCommandBuffer(cb);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cb;

    if (vkQueueSubmit(engine->ctx.graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS) {
        LOG_ERROR("ibl", "Failed to submit command buffer");
    }

    if (fence == VK_NULL_HANDLE) {
        {
            SVK_TRACY_FIBER_ZONE_C(fiberSync, "Hybrid Perf", "Sync (GPU Wait)", tracy_color::FiberHybridGpuWait);
            vkQueueWaitIdle(engine->ctx.graphicsQueue);
        }
        vkFreeCommandBuffers(engine->ctx.device, engine->ctx.commandPool, 1, &cb);
    }
}

float half_to_float(uint16_t h) {
    uint32_t sign = (h >> 15) & 0x00000001;
    uint32_t exp = (h >> 10) & 0x0000001F;
    uint32_t mant = h & 0x000003FF;

    if (exp == 0) {
        if (mant == 0) {
            uint32_t res = sign << 31;
            float f;
            memcpy(&f, &res, 4);
            return f;
        }
        while (!(mant & 0x00000400)) {
            mant <<= 1;
            exp--;
        }
        exp++;
        mant &= ~0x00000400;
    } else if (exp == 31) {
        if (mant == 0) {
            uint32_t res = (sign << 31) | 0x7F800000;
            float f;
            memcpy(&f, &res, 4);
            return f;
        }
        uint32_t res = (sign << 31) | 0x7F800000 | (mant << 13);
        float f;
        memcpy(&f, &res, 4);
        return f;
    }

    exp = exp + (127 - 15);
    mant = mant << 13;
    uint32_t res = (sign << 31) | (exp << 23) | mant;
    float f;
    memcpy(&f, &res, 4);
    return f;
}

ResourceResult save_image_as_hdr(VulkanEngine* engine, VkImage image, uint32_t width, uint32_t height, VkFormat format, uint32_t mip, const char* filename) {
    if (!engine || image == VK_NULL_HANDLE)
        return ResourceResult::ErrorInvalidFormat;
    int channels = 4;
    if (format == VK_FORMAT_R16G16_SFLOAT)
        channels = 2;

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * channels * 2;
    VkBuffer readbackBuffer = VK_NULL_HANDLE;
    VmaAllocation readbackAllocation = VK_NULL_HANDLE;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

    if (vmaCreateBuffer(engine->ctx.allocator, &bufferInfo, &allocInfo, &readbackBuffer, &readbackAllocation, nullptr) != VK_SUCCESS) {
        LOG_ERROR("ibl", "Failed to create readback buffer");
        return ResourceResult::ErrorInvalidFormat;
    }

    VkCommandBuffer cb = begin_single_time_commands(engine);
    if (!cb) {
        vmaDestroyBuffer(engine->ctx.allocator, readbackBuffer, readbackAllocation);
        return ResourceResult::ErrorInvalidFormat;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1};
    ((VulkanRHI*)engine->appState->rhi)
        ->CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
    region.imageExtent = {width, height, 1};
    ((VulkanRHI*)engine->appState->rhi)->CmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readbackBuffer, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    ((VulkanRHI*)engine->appState->rhi)
        ->CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &barrier);

    end_single_time_commands(engine, cb);

    void* mappedData = nullptr;
    if (vmaMapMemory(engine->ctx.allocator, readbackAllocation, &mappedData) != VK_SUCCESS) {
        LOG_ERROR("ibl", "Failed to map readback buffer");
        vmaDestroyBuffer(engine->ctx.allocator, readbackBuffer, readbackAllocation);
        return ResourceResult::ErrorInvalidFormat;
    }
    uint16_t* halfData = static_cast<uint16_t*>(mappedData);
    size_t numElements = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
    engine->iblBaker.m_exportFloatData.resize(numElements);

    for (size_t i = 0; i < numElements; ++i) {
        engine->iblBaker.m_exportFloatData[i] = half_to_float(halfData[i]);
    }
    vmaUnmapMemory(engine->ctx.allocator, readbackAllocation);

    int writeChannels = 3;
    if (channels == 4) {
        engine->iblBaker.m_exportFinalData.resize((size_t)width * height * 3);
        for (size_t i = 0; i < (size_t)width * height; ++i) {
            engine->iblBaker.m_exportFinalData[(i * 3) + 0] = engine->iblBaker.m_exportFloatData[(i * 4) + 0];
            engine->iblBaker.m_exportFinalData[(i * 3) + 1] = engine->iblBaker.m_exportFloatData[(i * 4) + 1];
            engine->iblBaker.m_exportFinalData[(i * 3) + 2] = engine->iblBaker.m_exportFloatData[(i * 4) + 2];
        }
    } else if (channels == 2) {
        engine->iblBaker.m_exportFinalData.resize((size_t)width * height * 3);
        for (size_t i = 0; i < (size_t)width * height; ++i) {
            engine->iblBaker.m_exportFinalData[(i * 3) + 0] = engine->iblBaker.m_exportFloatData[(i * 2) + 0];
            engine->iblBaker.m_exportFinalData[(i * 3) + 1] = engine->iblBaker.m_exportFloatData[(i * 2) + 1];
            engine->iblBaker.m_exportFinalData[(i * 3) + 2] = 0.0f;
        }
    } else {
        engine->iblBaker.m_exportFinalData = engine->iblBaker.m_exportFloatData;
    }

    bool ok = stbi_write_hdr(filename, (int)width, (int)height, writeChannels, engine->iblBaker.m_exportFinalData.data());
    vmaDestroyBuffer(engine->ctx.allocator, readbackBuffer, readbackAllocation);
    return ok ? ResourceResult::Success : ResourceResult::ErrorInvalidFormat;
}

void transition_image_layout(VulkanEngine* engine, VkCommandBuffer cb, VkImage image, uint32_t mipLevels, VkImageLayout oldLayout, VkImageLayout newLayout,
                             VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    ((VulkanRHI*)engine->appState->rhi)->CmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

std::vector<uint32_t> read_shader_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        LOG_ERROR("ibl", "Failed to open shader file: %s", path);
        return {};
    }
    fseek(f, 0, SEEK_END);
    size_t size = static_cast<size_t>(ftell(f));
    fseek(f, 0, SEEK_SET);
    std::vector<uint32_t> buffer(size / 4);
    if (fread(buffer.data(), 1, size, f) != size) {
        LOG_ERROR("ibl", "Failed to read shader file: %s", path);
        fclose(f);
        return {};
    }
    fclose(f);
    return buffer;
}

} // namespace

static GfxResult init_ibl_resources(VulkanEngine* engine) {
    {
        if (engine->iblBaker.irradianceMap.is_valid()) {
            engine->iblBaker.pendingOldTextures.push_back(std::move(engine->iblBaker.irradianceMap));
        }
        engine->iblBaker.irradianceMap.Reset(
            engine->appState->rhi,
            engine->appState->rhi->CreateTexture(IBL_IRM_SIZE, IBL_IRM_SIZE, TextureFormat::RGBA16_SFLOAT, TextureUsage::Storage, 1, "IBL_IrradianceMap"));
        if (!engine->iblBaker.irradianceMap.is_valid())
            return GfxResult::ErrorInitializationFailed;
    }
    {
        if (engine->iblBaker.prefilteredMap.is_valid()) {
            engine->iblBaker.pendingOldTextures.push_back(std::move(engine->iblBaker.prefilteredMap));
        }
        engine->iblBaker.prefilteredMap.Reset(engine->appState->rhi,
                                              engine->appState->rhi->CreateTexture(IBL_SPM_SIZE, IBL_SPM_SIZE, TextureFormat::RGBA16_SFLOAT,
                                                                                   TextureUsage::Storage, IBL_SPM_MIPS, "IBL_PrefilteredMap"));
        if (!engine->iblBaker.prefilteredMap.is_valid())
            return GfxResult::ErrorInitializationFailed;
    }
    {
        engine->iblBaker.brdfLut.Reset(engine->appState->rhi, engine->appState->rhi->CreateTexture(IBL_BRDF_SIZE, IBL_BRDF_SIZE, TextureFormat::RG16_SFLOAT,
                                                                                                   TextureUsage::Storage, 1, "IBL_BrdfLut"));
        if (!engine->iblBaker.brdfLut.is_valid())
            return GfxResult::ErrorInitializationFailed;
    }
    {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = IBL_MAX_GROUPS * sizeof(float);
        info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateBuffer(engine->ctx.allocator, &info, &allocInfo, &engine->iblBaker.lumGroupSumsBuffer, &engine->iblBaker.lumGroupSumsAllocation,
                            nullptr) != VK_SUCCESS)
            return GfxResult::ErrorInitializationFailed;
        info.size = sizeof(float);
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
        if (vmaCreateBuffer(engine->ctx.allocator, &info, &allocInfo, &engine->iblBaker.lumMeanBuffer, &engine->iblBaker.lumMeanAllocation, nullptr) !=
            VK_SUCCESS)
            return GfxResult::ErrorInitializationFailed;
    }
    return GfxResult::Success;
}

static GfxResult init_ibl_pipelines(VulkanEngine* engine) {
    engine->iblBaker.irradianceSampler.Reset(engine->appState->rhi, engine->appState->rhi->CreateSampler(1, true, "IBL_IrrSampler"));
    engine->iblBaker.prefilteredSampler.Reset(engine->appState->rhi,
                                              engine->appState->rhi->CreateSampler(13, true, "IBL_PrefSampler")); // maxLod 12 means 13 levels
    engine->iblBaker.brdfLutSampler.Reset(engine->appState->rhi, engine->appState->rhi->CreateSampler(1, true, "IBL_BrdfSampler"));
    if (!engine->iblBaker.irradianceSampler.is_valid() || !engine->iblBaker.prefilteredSampler.is_valid() || !engine->iblBaker.brdfLutSampler.is_valid())
        return GfxResult::ErrorInitializationFailed;

    {
        DescriptorSetLayoutBinding b[2] = {{0, DescriptorType::CombinedImageSampler, 1, ShaderStage::Compute},
                                           {1, DescriptorType::StorageImage, 1, ShaderStage::Compute}};
        DescriptorLayoutDesc desc1{b, 2};
        engine->iblBaker.iblDescriptorSetLayout.Reset(engine->appState->rhi, engine->appState->rhi->CreateDescriptorLayout(desc1));
        if (!engine->iblBaker.iblDescriptorSetLayout.is_valid())
            return GfxResult::ErrorInitializationFailed;

        b[1] = {1, DescriptorType::StorageBuffer, 1, ShaderStage::Compute};
        DescriptorLayoutDesc desc2{b, 2};
        engine->iblBaker.lum1DescriptorSetLayout.Reset(engine->appState->rhi, engine->appState->rhi->CreateDescriptorLayout(desc2));
        if (!engine->iblBaker.lum1DescriptorSetLayout.is_valid())
            return GfxResult::ErrorInitializationFailed;

        DescriptorSetLayoutBinding b2[2] = {{0, DescriptorType::StorageBuffer, 1, ShaderStage::Compute},
                                            {1, DescriptorType::StorageBuffer, 1, ShaderStage::Compute}};
        DescriptorLayoutDesc desc3{b2, 2};
        engine->iblBaker.lum2DescriptorSetLayout.Reset(engine->appState->rhi, engine->appState->rhi->CreateDescriptorLayout(desc3));
        if (!engine->iblBaker.lum2DescriptorSetLayout.is_valid())
            return GfxResult::ErrorInitializationFailed;
    }
    {
        PushConstantRange r{ShaderStage::Compute, 0, 64};
        DescriptorLayoutHandle l1[] = {engine->iblBaker.iblDescriptorSetLayout};
        PipelineLayoutDesc plDesc1{l1, 1, &r, 1};
        engine->iblBaker.iblPipelineLayout.Reset(engine->appState->rhi, engine->appState->rhi->CreatePipelineLayout(plDesc1, "IBL_BaseLayout"));

        DescriptorLayoutHandle l2[] = {engine->iblBaker.lum1DescriptorSetLayout};
        PipelineLayoutDesc plDesc2{l2, 1, &r, 1};
        engine->iblBaker.lum1PipelineLayout.Reset(engine->appState->rhi, engine->appState->rhi->CreatePipelineLayout(plDesc2, "IBL_Lum1Layout"));

        DescriptorLayoutHandle l3[] = {engine->iblBaker.lum2DescriptorSetLayout};
        PipelineLayoutDesc plDesc3{l3, 1, &r, 1};
        engine->iblBaker.lum2PipelineLayout.Reset(engine->appState->rhi, engine->appState->rhi->CreatePipelineLayout(plDesc3, "IBL_Lum2Layout"));

        if (!engine->iblBaker.iblPipelineLayout.is_valid() || !engine->iblBaker.lum1PipelineLayout.is_valid() ||
            !engine->iblBaker.lum2PipelineLayout.is_valid())
            return GfxResult::ErrorInitializationFailed;
    }
    {
        auto lumPass1Shader = read_shader_file("shaders/ibl_lum_pass1.spv");
        auto lumPass2Shader = read_shader_file("shaders/ibl_lum_pass2.spv");
        auto irradianceShader = read_shader_file("shaders/ibl_irmap.spv");
        auto specularShader = read_shader_file("shaders/ibl_spmap.spv");
        auto brdfLutShader = read_shader_file("shaders/ibl_spbrdf.spv");

        if (lumPass1Shader.empty() || lumPass2Shader.empty() || irradianceShader.empty() || specularShader.empty() || brdfLutShader.empty())
            return GfxResult::ErrorInitializationFailed;

        struct PipeDesc {
            const std::vector<uint32_t>& shader;
            PipelineLayoutHandle layout;
            rhi::PipelinePtr* outPipeline;
            const char* debugName;
        };
        PipeDesc descs[] = {{lumPass1Shader, engine->iblBaker.lum1PipelineLayout, &engine->iblBaker.lum1Pipeline, "IBL_Lum1"},
                            {lumPass2Shader, engine->iblBaker.lum2PipelineLayout, &engine->iblBaker.lum2Pipeline, "IBL_Lum2"},
                            {irradianceShader, engine->iblBaker.iblPipelineLayout, &engine->iblBaker.irmapPipeline, "IBL_Irr"},
                            {specularShader, engine->iblBaker.iblPipelineLayout, &engine->iblBaker.spmapPipeline, "IBL_Spec"},
                            {brdfLutShader, engine->iblBaker.iblPipelineLayout, &engine->iblBaker.brdfLutPipeline, "IBL_Brdf"}};

        for (const auto& d : descs) {
            ComputePipelineDesc cpDesc{d.layout, d.shader.data(), d.shader.size() * 4, "main", d.debugName};
            d.outPipeline->Reset(engine->appState->rhi, engine->appState->rhi->CreateComputePipeline(cpDesc));
            if (*(d.outPipeline) == INVALID_HANDLE)
                return GfxResult::ErrorInitializationFailed;
        }
    }
    return GfxResult::Success;
}

GfxResult init_ibl(VulkanEngine* engine) {
    LOG_INFO("ibl", "Initializing IBL module...");
    if (init_ibl_resources(engine) != GfxResult::Success)
        return GfxResult::ErrorInitializationFailed;
    if (init_ibl_pipelines(engine) != GfxResult::Success)
        return GfxResult::ErrorInitializationFailed;
    {
        DescriptorPoolSize sizes[3] = {{DescriptorType::CombinedImageSampler, 10}, {DescriptorType::StorageImage, 20}, {DescriptorType::StorageBuffer, 20}};
        DescriptorPoolDesc desc{sizes, 3, 10};
        engine->iblBaker.computeDescriptorPool.Reset(engine->appState->rhi, engine->appState->rhi->CreateDescriptorPool(desc));
        if (!engine->iblBaker.computeDescriptorPool.is_valid())
            return GfxResult::ErrorInitializationFailed;

        DescriptorLayoutHandle layouts[5] = {engine->iblBaker.lum1DescriptorSetLayout, engine->iblBaker.lum2DescriptorSetLayout,
                                             engine->iblBaker.iblDescriptorSetLayout, engine->iblBaker.iblDescriptorSetLayout,
                                             engine->iblBaker.iblDescriptorSetLayout};

        DescriptorSetAllocateDesc ai{};
        ai.pool = engine->iblBaker.computeDescriptorPool;
        ai.setCount = 5;
        ai.layouts = layouts;

        DescriptorSetHandle sets[5];
        if (engine->appState->rhi->AllocateDescriptorSets(ai, sets) != RHIResult::Success)
            return GfxResult::ErrorInitializationFailed;
        engine->iblBaker.lum1DescriptorSet = sets[0];
        engine->iblBaker.lum2DescriptorSet = sets[1];
        engine->iblBaker.irmapDescriptorSet = sets[2];
        engine->iblBaker.spmapDescriptorSet = sets[3];
        engine->iblBaker.brdfLutDescriptorSet = sets[4];
    }
    engine->iblBaker.brdfLutBaked = false;
    engine->iblBaker.bakedMeanLuminance = 1.0f;
    return GfxResult::Success;
}

void cleanup_ibl(VulkanEngine* engine) {
    if (engine->ctx.device == VK_NULL_HANDLE || engine->ctx.allocator == VK_NULL_HANDLE)
        return;
    engine->iblBaker.cleanupPendingResources(engine);
    if (engine->iblBaker.lumGroupSumsBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(engine->ctx.allocator, engine->iblBaker.lumGroupSumsBuffer, engine->iblBaker.lumGroupSumsAllocation);
        engine->iblBaker.lumGroupSumsBuffer = VK_NULL_HANDLE;
    }
    if (engine->iblBaker.lumMeanBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(engine->ctx.allocator, engine->iblBaker.lumMeanBuffer, engine->iblBaker.lumMeanAllocation);
        engine->iblBaker.lumMeanBuffer = VK_NULL_HANDLE;
    }
    if (engine->iblBaker.iblBakeFence != VK_NULL_HANDLE) {
        vkDestroyFence(engine->ctx.device, engine->iblBaker.iblBakeFence, nullptr);
        engine->iblBaker.iblBakeFence = VK_NULL_HANDLE;
    }
    engine->iblBaker.currentStagingBuffer = VK_NULL_HANDLE;
    LOG_INFO("ibl", "IBL resources cleaned up");
}

void vk_ibl_bake_luminance(VulkanEngine* engine) {
    SVK_TRACY_ZONE_SCOPED_C("IBL Bake: Luminance Pass", tracy_color::GpuComputeLum);
    SVK_TRACY_FIBER_ZONE_C(fiberHost, "Hybrid Perf", "Host (CPU): Luminance", tracy_color::FiberHybridCpu);
    if (!engine || engine->ctx.device == VK_NULL_HANDLE || engine->ctx.allocator == VK_NULL_HANDLE || engine->ctx.commandPool == VK_NULL_HANDLE) {
        LOG_WARNING("ibl", "vk_ibl_bake_luminance: engine not fully initialized, skipping bake.");
        return;
    }
    VkImageView vkEnvHdrImageView = ((VulkanRHI*)engine->appState->rhi)->GetVkImageView(engine->envHdrImage);
    VkSampler vkEnvHdrSampler = engine->envHdrSampler.is_valid() ? ((VulkanRHI*)engine->appState->rhi)->GetVkSampler(engine->envHdrSampler) : VK_NULL_HANDLE;

    if (!engine->envHdrImage.is_valid() || vkEnvHdrImageView == VK_NULL_HANDLE) {
        LOG_WARNING("ibl", "vk_ibl_bake_luminance: envHdr resources not ready, skipping bake.");
        return;
    }
    LOG_INFO("ibl", "Baking IBL maps for %ux%u environment map (Async Phase 1: Luminance)...", engine->envHdrWidth, engine->envHdrHeight);

    engine->iblBaker.bakeState = IblBakeState::Luminance;

    VkCommandBuffer cb = begin_single_time_commands(engine);
    if (!cb)
        return;
    tracy_vk_collect(engine, cb);

    transition_image_layout(engine, cb, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->iblBaker.irradianceMap), 1, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    transition_image_layout(engine, cb, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->iblBaker.prefilteredMap), IBL_SPM_MIPS,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    if (!engine->iblBaker.brdfLutBaked) {
        transition_image_layout(engine, cb, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->iblBaker.brdfLut), 1, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    uint32_t dX = (engine->envHdrWidth + 15) / 16;
    uint32_t dY = (engine->envHdrHeight + 15) / 16;
    uint32_t nG = std::min(dX * dY, IBL_MAX_GROUPS);

    {
        vk_begin_label(engine->ctx.device, cb, "IBL_Luminance_Pass", 0.8f, 0.8f, 0.2f);
        SVK_TRACY_VK_ZONE_C(gpuIblLumZone, engine, cb, "GPU IBL Luminance", tracy_color::GpuComputeLum);

        VulkanCommandList cmdList((VulkanRHI*)engine->appState->rhi, cb);
        cmdList.BindPipeline(engine->iblBaker.lum1Pipeline, true);
        VkDescriptorImageInfo envHdrImageInfo{vkEnvHdrSampler, vkEnvHdrImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo lumGroupBufferInfo{engine->iblBaker.lumGroupSumsBuffer, 0, nG * sizeof(float)};

        VkDescriptorSet vkLum1Set = ((VulkanRHI*)engine->appState->rhi)->GetVkDescriptorSet(engine->iblBaker.lum1DescriptorSet);
        VkWriteDescriptorSet ws[2] = {};
        ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[0].dstSet = vkLum1Set;
        ws[0].dstBinding = 0;
        ws[0].descriptorCount = 1;
        ws[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ws[0].pImageInfo = &envHdrImageInfo;

        ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[1].dstSet = vkLum1Set;
        ws[1].dstBinding = 1;
        ws[1].descriptorCount = 1;
        ws[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ws[1].pBufferInfo = &lumGroupBufferInfo;

        vkUpdateDescriptorSets(engine->ctx.device, 2, ws, 0, nullptr);
        cmdList.BindDescriptorSets(engine->iblBaker.lum1PipelineLayout, 0, 1, &engine->iblBaker.lum1DescriptorSet, true);
        cmdList.Dispatch(dX, dY, 1);

        VkBufferMemoryBarrier bar{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                  nullptr,
                                  VK_ACCESS_SHADER_WRITE_BIT,
                                  VK_ACCESS_SHADER_READ_BIT,
                                  VK_QUEUE_FAMILY_IGNORED,
                                  VK_QUEUE_FAMILY_IGNORED,
                                  engine->iblBaker.lumGroupSumsBuffer,
                                  0,
                                  nG * sizeof(float)};
        ((VulkanRHI*)engine->appState->rhi)
            ->CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &bar, 0, nullptr);

        cmdList.BindPipeline(engine->iblBaker.lum2Pipeline, true);
        VkDescriptorBufferInfo meanLumBufferInfo{engine->iblBaker.lumMeanBuffer, 0, sizeof(float)};

        VkDescriptorSet vkLum2Set = ((VulkanRHI*)engine->appState->rhi)->GetVkDescriptorSet(engine->iblBaker.lum2DescriptorSet);
        VkWriteDescriptorSet ws2[2] = {};
        ws2[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws2[0].dstSet = vkLum2Set;
        ws2[0].dstBinding = 0;
        ws2[0].descriptorCount = 1;
        ws2[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ws2[0].pBufferInfo = &lumGroupBufferInfo;

        ws2[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws2[1].dstSet = vkLum2Set;
        ws2[1].dstBinding = 1;
        ws2[1].descriptorCount = 1;
        ws2[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ws2[1].pBufferInfo = &meanLumBufferInfo;

        vkUpdateDescriptorSets(engine->ctx.device, 2, ws2, 0, nullptr);
        struct {
            uint32_t g;
            uint32_t p;
        } pc = {nG, engine->envHdrWidth * engine->envHdrHeight};
        cmdList.PushConstants(engine->iblBaker.lum2PipelineLayout, ShaderStage::Compute, 0, sizeof(pc), &pc);
        cmdList.BindDescriptorSets(engine->iblBaker.lum2PipelineLayout, 0, 1, &engine->iblBaker.lum2DescriptorSet, true);
        cmdList.Dispatch(1, 1, 1);

        vk_end_label(engine->ctx.device, cb);
    }

    vk_ibl_reset_bake_fence(engine);

    engine->iblBaker.iblBakeCommandBuffer = cb;
    end_single_time_commands(engine, cb, engine->iblBaker.iblBakeFence);
    LOG_INFO("ibl", "Luminance pass dispatched asynchronously.");
}

void vk_ibl_bake_brdf(VulkanEngine* engine) {
    SVK_TRACY_ZONE_SCOPED_C("IBL Bake: BRDF LUT Pass", tracy_color::GpuComputeBrdf);
    SVK_TRACY_FIBER_ZONE_C(fiberHost, "Hybrid Perf", "Host (CPU): BRDF LUT", tracy_color::FiberHybridCpu);
    if (engine->iblBaker.brdfLutBaked) {
        if (engine->iblBaker.iblBakeFence != VK_NULL_HANDLE) {
            vkDestroyFence(engine->ctx.device, engine->iblBaker.iblBakeFence, nullptr);
            engine->iblBaker.iblBakeFence = VK_NULL_HANDLE;
        }
        engine->iblBaker.bakeState = IblBakeState::BrdfWait;
        return;
    }

    VkCommandBuffer cb = begin_single_time_commands(engine);
    if (!cb)
        return;
    tracy_vk_collect(engine, cb);

    vk_begin_label(engine->ctx.device, cb, "IBL_Bake_BRDF", 0.3f, 0.6f, 0.9f);
    VulkanCommandList cmdList2((VulkanRHI*)engine->appState->rhi, cb);

    {
        SVK_TRACY_VK_ZONE_C(gpuIblBrdfZone, engine, cb, "GPU IBL BRDF LUT", tracy_color::GpuComputeBrdf);
        cmdList2.BindPipeline(engine->iblBaker.brdfLutPipeline, true);

        DescriptorImageInfo inputImage{engine->envHdrSampler, INVALID_HANDLE, engine->envHdrImage, TextureLayout::ShaderReadOnlyOptimal};
        DescriptorImageInfo outputImage{INVALID_HANDLE, INVALID_HANDLE, engine->iblBaker.brdfLut, TextureLayout::General};

        WriteDescriptorSet writeSets[2] = {};
        writeSets[0].dstSet = engine->iblBaker.brdfLutDescriptorSet;
        writeSets[0].dstBinding = 0;
        writeSets[0].dstArrayElement = 0;
        writeSets[0].descriptorCount = 1;
        writeSets[0].descriptorType = DescriptorType::CombinedImageSampler;
        writeSets[0].pImageInfo = &inputImage;

        writeSets[1].dstSet = engine->iblBaker.brdfLutDescriptorSet;
        writeSets[1].dstBinding = 1;
        writeSets[1].dstArrayElement = 0;
        writeSets[1].descriptorCount = 1;
        writeSets[1].descriptorType = DescriptorType::StorageImage;
        writeSets[1].pImageInfo = &outputImage;

        engine->appState->rhi->UpdateDescriptorSets(2, writeSets);
        cmdList2.BindDescriptorSets(engine->iblBaker.iblPipelineLayout, 0, 1, &engine->iblBaker.brdfLutDescriptorSet, true);
        cmdList2.Dispatch(IBL_BRDF_SIZE / 32, IBL_BRDF_SIZE / 32, 1);

        transition_image_layout(engine, cb, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->iblBaker.brdfLut), 1, VK_IMAGE_LAYOUT_GENERAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }

    vk_end_label(engine->ctx.device, cb);

    vk_ibl_reset_bake_fence(engine);

    engine->iblBaker.iblBakeCommandBuffer = cb;
    end_single_time_commands(engine, cb, engine->iblBaker.iblBakeFence);
    engine->iblBaker.brdfLutBaked = true;

    LOG_INFO("ibl", "BRDF LUT pass dispatched asynchronously.");
}

void vk_ibl_bake_irradiance(VulkanEngine* engine) {
    SVK_TRACY_ZONE_SCOPED_C("IBL Bake: Irradiance Pass", tracy_color::GpuComputeIrr);
    SVK_TRACY_FIBER_ZONE_C(fiberHost, "Hybrid Perf", "Host (CPU): Irradiance", tracy_color::FiberHybridCpu);
    float threshold = engine->iblBaker.bakedMeanLuminance * 3.0f;

    VkCommandBuffer cb = begin_single_time_commands(engine);
    if (!cb)
        return;
    tracy_vk_collect(engine, cb);

    vk_begin_label(engine->ctx.device, cb, "IBL_Bake_Irradiance", 0.9f, 0.6f, 0.3f);
    VulkanCommandList cmdList2((VulkanRHI*)engine->appState->rhi, cb);

    if (engine->iblBaker.currentSlice == 0) {
        transition_image_layout(engine, cb, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->iblBaker.irradianceMap), 1, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    {
        SVK_TRACY_VK_ZONE_C(gpuIblIrrZone, engine, cb, "GPU IBL Irradiance Slice", tracy_color::GpuComputeIrr);
        LOG_INFO("ibl", "Baking Irradiance Map (Slice %d/%d)...", engine->iblBaker.currentSlice + 1, engine->iblBaker.totalSlices);

        cmdList2.BindPipeline(engine->iblBaker.irmapPipeline, true);
        DescriptorImageInfo inputImage{engine->envHdrSampler, INVALID_HANDLE, engine->envHdrImage, TextureLayout::ShaderReadOnlyOptimal};
        DescriptorImageInfo outputImage{INVALID_HANDLE, INVALID_HANDLE, engine->iblBaker.irradianceMap, TextureLayout::General};

        WriteDescriptorSet writeSets[2] = {};
        writeSets[0].dstSet = engine->iblBaker.irmapDescriptorSet;
        writeSets[0].dstBinding = 0;
        writeSets[0].dstArrayElement = 0;
        writeSets[0].descriptorCount = 1;
        writeSets[0].descriptorType = DescriptorType::CombinedImageSampler;
        writeSets[0].pImageInfo = &inputImage;

        writeSets[1].dstSet = engine->iblBaker.irmapDescriptorSet;
        writeSets[1].dstBinding = 1;
        writeSets[1].dstArrayElement = 0;
        writeSets[1].descriptorCount = 1;
        writeSets[1].descriptorType = DescriptorType::StorageImage;
        writeSets[1].pImageInfo = &outputImage;

        engine->appState->rhi->UpdateDescriptorSets(2, writeSets);

        int lines_per_slice = ((int)IBL_IRM_SIZE + engine->iblBaker.totalSlices - 1) / engine->iblBaker.totalSlices;
        int start_y = engine->iblBaker.currentSlice * lines_per_slice;
        int end_y = std::min((int)IBL_IRM_SIZE, start_y + lines_per_slice);
        int actual_lines = end_y - start_y;

        if (actual_lines > 0) {
            struct {
                float t;
                int oy;
                int my;
            } pc = {threshold, start_y, end_y};
            cmdList2.PushConstants(engine->iblBaker.iblPipelineLayout, ShaderStage::Compute, 0, sizeof(pc), &pc);
            cmdList2.BindDescriptorSets(engine->iblBaker.iblPipelineLayout, 0, 1, &engine->iblBaker.irmapDescriptorSet, true);

            cmdList2.Dispatch(IBL_IRM_SIZE / 32, (actual_lines + 31) / 32, 1);
        }

        if (engine->iblBaker.currentSlice == engine->iblBaker.totalSlices - 1) {
            transition_image_layout(engine, cb, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->iblBaker.irradianceMap), 1, VK_IMAGE_LAYOUT_GENERAL,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }
    }

    vk_end_label(engine->ctx.device, cb);

    vk_ibl_reset_bake_fence(engine);

    engine->iblBaker.iblBakeCommandBuffer = cb;
    end_single_time_commands(engine, cb, engine->iblBaker.iblBakeFence);
}

void vk_ibl_bake_prefilter(VulkanEngine* engine) {
    SVK_TRACY_ZONE_SCOPED_C("IBL Bake: Specular Prefilter Pass", tracy_color::GpuComputeSpec);
    SVK_TRACY_FIBER_ZONE_C(fiberHost, "Hybrid Perf", "Host (CPU): Specular", tracy_color::FiberHybridCpu);
    float threshold = engine->iblBaker.bakedMeanLuminance * 3.0f;

    VkCommandBuffer cb = begin_single_time_commands(engine);
    if (!cb)
        return;
    tracy_vk_collect(engine, cb);

    vk_begin_label(engine->ctx.device, cb, "IBL_Bake_Prefilter", 0.3f, 0.6f, 0.9f);

    VulkanCommandList cmdList2((VulkanRHI*)engine->appState->rhi, cb);

    if (engine->iblBaker.currentMip == 0) {
        transition_image_layout(engine, cb, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->iblBaker.prefilteredMap), IBL_SPM_MIPS,
                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    ImageViewHandle finalViewHandle = INVALID_HANDLE;
    DescriptorPoolHandle poolHandle = INVALID_HANDLE;

    {
        SVK_TRACY_VK_ZONE_C(gpuIblSpecZone, engine, cb, "GPU IBL Specular", tracy_color::GpuComputeSpec);

        if (engine->iblBaker.currentSlice == 0 && engine->iblBaker.currentMip == 0) {
            LOG_INFO("ibl", "Pass 2: Specular Map...");
        }
        cmdList2.BindPipeline(engine->iblBaker.spmapPipeline, true);

        DescriptorPoolSize ps[2] = {{DescriptorType::CombinedImageSampler, 1}, {DescriptorType::StorageImage, 1}};
        DescriptorPoolDesc desc{ps, 2, 1};
        poolHandle = engine->appState->rhi->CreateDescriptorPool(desc);
        if (poolHandle != INVALID_HANDLE) {
            DescriptorLayoutHandle layouts[1] = {engine->iblBaker.iblDescriptorSetLayout};
            DescriptorSetAllocateDesc ai{};
            ai.pool = poolHandle;
            ai.setCount = 1;
            ai.layouts = layouts;

            DescriptorSetHandle sets[1] = {INVALID_HANDLE};
            if (engine->appState->rhi->AllocateDescriptorSets(ai, sets) == RHIResult::Success) {
                uint32_t i = engine->iblBaker.currentMip;
                ImageViewHandle viewHandle = engine->appState->rhi->CreateImageView(engine->iblBaker.prefilteredMap, i, 1, 0, 1);
                finalViewHandle = viewHandle;

                DescriptorImageInfo inputImage{engine->envHdrSampler, INVALID_HANDLE, engine->envHdrImage, TextureLayout::ShaderReadOnlyOptimal};
                DescriptorImageInfo outputImage{INVALID_HANDLE, viewHandle, INVALID_HANDLE, TextureLayout::General};

                WriteDescriptorSet writeSets[2] = {};
                writeSets[0].dstSet = sets[0];
                writeSets[0].dstBinding = 0;
                writeSets[0].dstArrayElement = 0;
                writeSets[0].descriptorCount = 1;
                writeSets[0].descriptorType = DescriptorType::CombinedImageSampler;
                writeSets[0].pImageInfo = &inputImage;
                writeSets[1].dstSet = sets[0];
                writeSets[1].dstBinding = 1;
                writeSets[1].dstArrayElement = 0;
                writeSets[1].descriptorCount = 1;
                writeSets[1].descriptorType = DescriptorType::StorageImage;
                writeSets[1].pImageInfo = &outputImage;

                engine->appState->rhi->UpdateDescriptorSets(2, writeSets);

                uint32_t sz = std::max(1u, IBL_SPM_SIZE >> i);
                int lines_per_slice = ((int)sz + engine->iblBaker.totalSlices - 1) / engine->iblBaker.totalSlices;
                int offsetY = engine->iblBaker.currentSlice * lines_per_slice;
                int maxY = std::min((int)sz, offsetY + lines_per_slice);
                int actual_lines = maxY - offsetY;

                if (actual_lines > 0) {
                    struct {
                        float roughness;
                        int mip;
                        float threshold;
                        int offsetY;
                        int maxY;
                    } pc = {(float)i / (float)(IBL_SPM_MIPS - 1), (int)i, threshold, offsetY, maxY};
                    cmdList2.PushConstants(engine->iblBaker.iblPipelineLayout, ShaderStage::Compute, 0, sizeof(pc), &pc);
                    cmdList2.BindDescriptorSets(engine->iblBaker.iblPipelineLayout, 0, 1, sets, true);

                    uint32_t groups_y = (actual_lines + 31) / 32;
                    cmdList2.Dispatch((sz + 31) / 32, groups_y, 1);
                }
            }
        }

        if (engine->iblBaker.currentMip == (int)IBL_SPM_MIPS - 1) {
            transition_image_layout(engine, cb, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->iblBaker.prefilteredMap), IBL_SPM_MIPS,
                                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }
    }

    vk_end_label(engine->ctx.device, cb);

    vk_ibl_reset_bake_fence(engine);

    engine->iblBaker.iblBakeCommandBuffer = cb;
    if (poolHandle != INVALID_HANDLE) {
        engine->iblBaker.pendingDescriptorPools.push_back((uint64_t)poolHandle);
    }
    if (finalViewHandle != INVALID_HANDLE) {
        engine->iblBaker.pendingImageViews.push_back((uint64_t)finalViewHandle);
    }

    end_single_time_commands(engine, cb, engine->iblBaker.iblBakeFence);

    if (engine->iblBaker.currentSlice == engine->iblBaker.totalSlices - 1 && engine->iblBaker.currentMip == (int)IBL_SPM_MIPS - 1) {
        LOG_INFO("ibl", "Fallback maps bake complete.");
    }
}

void IblBaker::ExportMaps(VulkanEngine* engine) const {
    const char* dump_dir = "/tmp/ibl_tests/vk";
    std::error_code ec;
    std::filesystem::create_directories(dump_dir, ec);
    if (ec) {
        LOG_ERROR("ibl", "Failed to create dump directory: %s", ec.message().c_str());
    }
    LOG_INFO("ibl", "Exporting IBL maps to HDR files in %s...", dump_dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/brdf_lut.hdr", dump_dir);
    (void)save_image_as_hdr(engine, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(this->brdfLut), IBL_BRDF_SIZE, IBL_BRDF_SIZE, VK_FORMAT_R16G16_SFLOAT, 0,
                            path);
    snprintf(path, sizeof(path), "%s/irradiance.hdr", dump_dir);
    (void)save_image_as_hdr(engine, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(this->irradianceMap), IBL_IRM_SIZE, IBL_IRM_SIZE,
                            VK_FORMAT_R16G16B16A16_SFLOAT, 0, path);
    for (uint32_t i = 0; i < IBL_SPM_MIPS; ++i) {
        uint32_t sz = std::max(1u, IBL_SPM_SIZE >> i);
        snprintf(path, sizeof(path), "%s/prefiltered_mip%u.hdr", dump_dir, i);
        (void)save_image_as_hdr(engine, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(this->prefilteredMap), sz, sz, VK_FORMAT_R16G16B16A16_SFLOAT, i, path);
    }
    LOG_DEBUG("ibl", "Export Complete.");
}

void vk_ibl_reset_bake_fence(VulkanEngine* engine) {
    if (engine->iblBaker.iblBakeFence != VK_NULL_HANDLE) {
        vkDestroyFence(engine->ctx.device, engine->iblBaker.iblBakeFence, nullptr);
    }
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(engine->ctx.device, &fenceInfo, nullptr, &engine->iblBaker.iblBakeFence);
    vk_set_object_name(engine->ctx.device, (uint64_t)engine->iblBaker.iblBakeFence, VK_OBJECT_TYPE_FENCE, "IBL_Bake_Fence");
}
void IblBaker::Cleanup() {
    computeDescriptorPool.Reset();

    irradianceMap.Reset();
    irradianceSampler.Reset();
    prefilteredMap.Reset();
    prefilteredSampler.Reset();
    brdfLut.Reset();
    brdfLutSampler.Reset();

    irmapPipeline.Reset();
    spmapPipeline.Reset();
    brdfLutPipeline.Reset();
    lum1Pipeline.Reset();
    lum2Pipeline.Reset();

    iblPipelineLayout.Reset();
    lum1PipelineLayout.Reset();
    lum2PipelineLayout.Reset();

    iblDescriptorSetLayout.Reset();
    lum1DescriptorSetLayout.Reset();
    lum2DescriptorSetLayout.Reset();
}
