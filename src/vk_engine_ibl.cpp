#include "vk_engine_ibl.h"
#include "app_log.h"
#include "rhi/vulkan_command_list.h"
#include "rhi/vulkan_rhi.h"
#include "tracy_client.h"
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
    if (!engine || engine->commandPool == VK_NULL_HANDLE) {
        LOG_ERROR("ibl", "begin_single_time_commands: engine or commandPool is NULL");
        return VK_NULL_HANDLE;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = engine->commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cb;
    if (vkAllocateCommandBuffers(engine->device, &allocInfo, &cb) != VK_SUCCESS) {
        LOG_ERROR("ibl", "Failed to allocate command buffer");
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cb, &beginInfo) != VK_SUCCESS) {
        LOG_ERROR("ibl", "Failed to begin command buffer");
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &cb);
        return VK_NULL_HANDLE;
    }

    return cb;
}

void end_single_time_commands(VulkanEngine* engine, VkCommandBuffer cb) {
    if (!engine || cb == VK_NULL_HANDLE)
        return;

    vkEndCommandBuffer(cb);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cb;

    if (vkQueueSubmit(engine->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        LOG_ERROR("ibl", "Failed to submit command buffer");
    }
    vkQueueWaitIdle(engine->graphicsQueue);

    vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &cb);
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

bool save_image_as_hdr(VulkanEngine* engine, VkImage image, uint32_t width, uint32_t height, VkFormat format, uint32_t mip, const char* filename) {
    if (!engine || image == VK_NULL_HANDLE)
        return false;
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

    if (vmaCreateBuffer(engine->allocator, &bufferInfo, &allocInfo, &readbackBuffer, &readbackAllocation, nullptr) != VK_SUCCESS) {
        LOG_ERROR("ibl", "Failed to create readback buffer");
        return false;
    }

    VkCommandBuffer cb = begin_single_time_commands(engine);
    if (!cb) {
        vmaDestroyBuffer(engine->allocator, readbackBuffer, readbackAllocation);
        return false;
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
    if (vmaMapMemory(engine->allocator, readbackAllocation, &mappedData) != VK_SUCCESS) {
        LOG_ERROR("ibl", "Failed to map readback buffer");
        vmaDestroyBuffer(engine->allocator, readbackBuffer, readbackAllocation);
        return false;
    }
    uint16_t* halfData = static_cast<uint16_t*>(mappedData);
    std::vector<float> floatData(static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels));

    for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels); ++i) {
        floatData[i] = half_to_float(halfData[i]);
    }
    vmaUnmapMemory(engine->allocator, readbackAllocation);

    int writeChannels = 3;
    std::vector<float> finalData;
    if (channels == 4) {
        finalData.resize((size_t)width * height * 3);
        for (size_t i = 0; i < (size_t)width * height; ++i) {
            finalData[(i * 3) + 0] = floatData[(i * 4) + 0];
            finalData[(i * 3) + 1] = floatData[(i * 4) + 1];
            finalData[(i * 3) + 2] = floatData[(i * 4) + 2];
        }
    } else if (channels == 2) {
        finalData.resize((size_t)width * height * 3);
        for (size_t i = 0; i < (size_t)width * height; ++i) {
            finalData[(i * 3) + 0] = floatData[(i * 2) + 0];
            finalData[(i * 3) + 1] = floatData[(i * 2) + 1];
            finalData[(i * 3) + 2] = 0.0f;
        }
    } else {
        finalData = std::move(floatData);
    }

    bool ok = stbi_write_hdr(filename, (int)width, (int)height, writeChannels, finalData.data());
    vmaDestroyBuffer(engine->allocator, readbackBuffer, readbackAllocation);
    return ok;
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

static bool init_ibl_resources(VulkanEngine* engine) {
    {
        engine->ibl.irradianceMap.Reset(engine->appState->rhi, engine->appState->rhi->CreateTexture(IBL_IRM_SIZE, IBL_IRM_SIZE, TextureFormat::RGBA16_SFLOAT,
                                                                                                    TextureUsage::Storage, 1, "IBL_IrradianceMap"));
        if (!engine->ibl.irradianceMap.is_valid())
            return false;
    }
    {
        engine->ibl.prefilteredMap.Reset(engine->appState->rhi,
                                         engine->appState->rhi->CreateTexture(IBL_SPM_SIZE, IBL_SPM_SIZE, TextureFormat::RGBA16_SFLOAT, TextureUsage::Storage,
                                                                              IBL_SPM_MIPS, "IBL_PrefilteredMap"));
        if (!engine->ibl.prefilteredMap.is_valid())
            return false;
    }
    {
        engine->ibl.brdfLut.Reset(engine->appState->rhi, engine->appState->rhi->CreateTexture(IBL_BRDF_SIZE, IBL_BRDF_SIZE, TextureFormat::RG16_SFLOAT,
                                                                                              TextureUsage::Storage, 1, "IBL_BrdfLut"));
        if (!engine->ibl.brdfLut.is_valid())
            return false;
    }
    {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = IBL_MAX_GROUPS * sizeof(float);
        info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateBuffer(engine->allocator, &info, &allocInfo, &engine->ibl.lumGroupSumsBuffer, &engine->ibl.lumGroupSumsAllocation, nullptr) != VK_SUCCESS)
            return false;
        info.size = sizeof(float);
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
        if (vmaCreateBuffer(engine->allocator, &info, &allocInfo, &engine->ibl.lumMeanBuffer, &engine->ibl.lumMeanAllocation, nullptr) != VK_SUCCESS)
            return false;
    }
    return true;
}

static bool init_ibl_pipelines(VulkanEngine* engine) {
    engine->ibl.irradianceSampler.Reset(engine->appState->rhi, engine->appState->rhi->CreateSampler(1, true, "IBL_IrrSampler"));
    engine->ibl.prefilteredSampler.Reset(engine->appState->rhi, engine->appState->rhi->CreateSampler(13, true, "IBL_PrefSampler")); // maxLod 12 means 13 levels
    engine->ibl.brdfLutSampler.Reset(engine->appState->rhi, engine->appState->rhi->CreateSampler(1, true, "IBL_BrdfSampler"));
    if (!engine->ibl.irradianceSampler.is_valid() || !engine->ibl.prefilteredSampler.is_valid() || !engine->ibl.brdfLutSampler.is_valid())
        return false;

    {
        DescriptorSetLayoutBinding b[2] = {{0, DescriptorType::CombinedImageSampler, 1, ShaderStage::Compute},
                                           {1, DescriptorType::StorageImage, 1, ShaderStage::Compute}};
        DescriptorLayoutDesc desc1{b, 2};
        engine->ibl.iblDescriptorSetLayout.Reset(engine->appState->rhi, engine->appState->rhi->CreateDescriptorLayout(desc1));
        if (!engine->ibl.iblDescriptorSetLayout.is_valid())
            return false;

        b[1] = {1, DescriptorType::StorageBuffer, 1, ShaderStage::Compute};
        DescriptorLayoutDesc desc2{b, 2};
        engine->ibl.lum1DescriptorSetLayout.Reset(engine->appState->rhi, engine->appState->rhi->CreateDescriptorLayout(desc2));
        if (!engine->ibl.lum1DescriptorSetLayout.is_valid())
            return false;

        DescriptorSetLayoutBinding b2[2] = {{0, DescriptorType::StorageBuffer, 1, ShaderStage::Compute},
                                            {1, DescriptorType::StorageBuffer, 1, ShaderStage::Compute}};
        DescriptorLayoutDesc desc3{b2, 2};
        engine->ibl.lum2DescriptorSetLayout.Reset(engine->appState->rhi, engine->appState->rhi->CreateDescriptorLayout(desc3));
        if (!engine->ibl.lum2DescriptorSetLayout.is_valid())
            return false;
    }
    {
        PushConstantRange r{ShaderStage::Compute, 0, 64};
        DescriptorLayoutHandle l1[] = {engine->ibl.iblDescriptorSetLayout};
        PipelineLayoutDesc plDesc1{l1, 1, &r, 1};
        engine->ibl.iblPipelineLayout.Reset(engine->appState->rhi, engine->appState->rhi->CreatePipelineLayout(plDesc1, "IBL_BaseLayout"));

        DescriptorLayoutHandle l2[] = {engine->ibl.lum1DescriptorSetLayout};
        PipelineLayoutDesc plDesc2{l2, 1, &r, 1};
        engine->ibl.lum1PipelineLayout.Reset(engine->appState->rhi, engine->appState->rhi->CreatePipelineLayout(plDesc2, "IBL_Lum1Layout"));

        DescriptorLayoutHandle l3[] = {engine->ibl.lum2DescriptorSetLayout};
        PipelineLayoutDesc plDesc3{l3, 1, &r, 1};
        engine->ibl.lum2PipelineLayout.Reset(engine->appState->rhi, engine->appState->rhi->CreatePipelineLayout(plDesc3, "IBL_Lum2Layout"));

        if (!engine->ibl.iblPipelineLayout.is_valid() || !engine->ibl.lum1PipelineLayout.is_valid() || !engine->ibl.lum2PipelineLayout.is_valid())
            return false;
    }
    {
        auto lumPass1Shader = read_shader_file("shaders/ibl_lum_pass1.spv");
        auto lumPass2Shader = read_shader_file("shaders/ibl_lum_pass2.spv");
        auto irradianceShader = read_shader_file("shaders/ibl_irmap.spv");
        auto specularShader = read_shader_file("shaders/ibl_spmap.spv");
        auto brdfLutShader = read_shader_file("shaders/ibl_spbrdf.spv");

        if (lumPass1Shader.empty() || lumPass2Shader.empty() || irradianceShader.empty() || specularShader.empty() || brdfLutShader.empty())
            return false;

        struct PipeDesc {
            const std::vector<uint32_t>& shader;
            PipelineLayoutHandle layout;
            rhi::PipelinePtr* outPipeline;
            const char* debugName;
        };
        PipeDesc descs[] = {{lumPass1Shader, engine->ibl.lum1PipelineLayout, &engine->ibl.lum1Pipeline, "IBL_Lum1"},
                            {lumPass2Shader, engine->ibl.lum2PipelineLayout, &engine->ibl.lum2Pipeline, "IBL_Lum2"},
                            {irradianceShader, engine->ibl.iblPipelineLayout, &engine->ibl.irmapPipeline, "IBL_Irr"},
                            {specularShader, engine->ibl.iblPipelineLayout, &engine->ibl.spmapPipeline, "IBL_Spec"},
                            {brdfLutShader, engine->ibl.iblPipelineLayout, &engine->ibl.brdfLutPipeline, "IBL_Brdf"}};

        for (const auto& d : descs) {
            ComputePipelineDesc cpDesc{d.layout, d.shader.data(), d.shader.size() * 4, "main", d.debugName};
            d.outPipeline->Reset(engine->appState->rhi, engine->appState->rhi->CreateComputePipeline(cpDesc));
            if (*(d.outPipeline) == INVALID_HANDLE)
                return false;
        }
    }
    return true;
}

bool init_ibl(VulkanEngine* engine) {
    LOG_INFO("ibl", "Initializing IBL module...");
    if (!init_ibl_resources(engine))
        return false;
    if (!init_ibl_pipelines(engine))
        return false;
    {
        DescriptorPoolSize sizes[3] = {{DescriptorType::CombinedImageSampler, 10}, {DescriptorType::StorageImage, 20}, {DescriptorType::StorageBuffer, 20}};
        DescriptorPoolDesc desc{sizes, 3, 10};
        engine->ibl.computeDescriptorPool.Reset(engine->appState->rhi, engine->appState->rhi->CreateDescriptorPool(desc));
        if (!engine->ibl.computeDescriptorPool.is_valid())
            return false;

        DescriptorLayoutHandle layouts[5] = {engine->ibl.lum1DescriptorSetLayout, engine->ibl.lum2DescriptorSetLayout, engine->ibl.iblDescriptorSetLayout,
                                             engine->ibl.iblDescriptorSetLayout, engine->ibl.iblDescriptorSetLayout};

        DescriptorSetAllocateDesc ai{};
        ai.pool = engine->ibl.computeDescriptorPool;
        ai.setCount = 5;
        ai.layouts = layouts;

        DescriptorSetHandle sets[5];
        if (!engine->appState->rhi->AllocateDescriptorSets(ai, sets))
            return false;
        engine->ibl.lum1DescriptorSet = sets[0];
        engine->ibl.lum2DescriptorSet = sets[1];
        engine->ibl.irmapDescriptorSet = sets[2];
        engine->ibl.spmapDescriptorSet = sets[3];
        engine->ibl.brdfLutDescriptorSet = sets[4];
    }
    engine->ibl.brdfLutBaked = false;
    engine->ibl.bakedMeanLuminance = 1.0f;
    return true;
}

void cleanup_ibl(VulkanEngine* engine) {
    if (engine->device == VK_NULL_HANDLE)
        return;
    vmaDestroyBuffer(engine->allocator, engine->ibl.lumGroupSumsBuffer, engine->ibl.lumGroupSumsAllocation);
    vmaDestroyBuffer(engine->allocator, engine->ibl.lumMeanBuffer, engine->ibl.lumMeanAllocation);
    LOG_INFO("ibl", "IBL resources cleaned up");
}

void vk_ibl_bake(VulkanEngine* engine) {
    SVK_TRACY_ZONE_SCOPED("vk_ibl_bake");
    if (!engine || engine->device == VK_NULL_HANDLE || engine->allocator == VK_NULL_HANDLE || engine->commandPool == VK_NULL_HANDLE) {
        LOG_WARNING("ibl", "vk_ibl_bake: engine not fully initialized, skipping bake.");
        return;
    }
    VkImageView vkEnvHdrImageView = ((VulkanRHI*)engine->appState->rhi)->GetVkImageView(engine->envHdrImage);
    VkSampler vkEnvHdrSampler = engine->envHdrSampler.is_valid() ? ((VulkanRHI*)engine->appState->rhi)->GetVkSampler(engine->envHdrSampler) : VK_NULL_HANDLE;

    if (!engine->envHdrImage.is_valid() || vkEnvHdrImageView == VK_NULL_HANDLE) {
        LOG_WARNING("ibl", "vk_ibl_bake: envHdr resources not ready, skipping bake.");
        return;
    }
    LOG_INFO("ibl", "Baking IBL maps for %ux%u environment map...", engine->envHdrWidth, engine->envHdrHeight);

    VkCommandBuffer cb = begin_single_time_commands(engine);
    if (!cb)
        return;
    tracy_vk_collect(engine, cb);

    transition_image_layout(engine, cb, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->ibl.irradianceMap), 1, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    transition_image_layout(engine, cb, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->ibl.prefilteredMap), IBL_SPM_MIPS, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    if (!engine->ibl.brdfLutBaked) {
        transition_image_layout(engine, cb, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->ibl.brdfLut), 1, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    uint32_t dX = (engine->envHdrWidth + 15) / 16;
    uint32_t dY = (engine->envHdrHeight + 15) / 16;
    uint32_t nG = std::min(dX * dY, IBL_MAX_GROUPS);

    {
        vk_begin_label(engine->device, cb, "IBL_Luminance_Pass", 0.8f, 0.8f, 0.2f);
        SVK_TRACY_VK_NAMED_ZONE(gpuIblLumZone, engine, cb, "GPU IBL Luminance");

        VulkanCommandList cmdList((VulkanRHI*)engine->appState->rhi, cb);
        cmdList.BindPipeline(engine->ibl.lum1Pipeline, true);
        VkDescriptorImageInfo envHdrImageInfo{vkEnvHdrSampler, vkEnvHdrImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo lumGroupBufferInfo{engine->ibl.lumGroupSumsBuffer, 0, nG * sizeof(float)};

        VkDescriptorSet vkLum1Set = ((VulkanRHI*)engine->appState->rhi)->GetVkDescriptorSet(engine->ibl.lum1DescriptorSet);
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

        vkUpdateDescriptorSets(engine->device, 2, ws, 0, nullptr);
        cmdList.BindDescriptorSets(engine->ibl.lum1PipelineLayout, 0, 1, &engine->ibl.lum1DescriptorSet, true);
        cmdList.Dispatch(dX, dY, 1);

        VkBufferMemoryBarrier bar{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                  nullptr,
                                  VK_ACCESS_SHADER_WRITE_BIT,
                                  VK_ACCESS_SHADER_READ_BIT,
                                  VK_QUEUE_FAMILY_IGNORED,
                                  VK_QUEUE_FAMILY_IGNORED,
                                  engine->ibl.lumGroupSumsBuffer,
                                  0,
                                  nG * sizeof(float)};
        ((VulkanRHI*)engine->appState->rhi)
            ->CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &bar, 0, nullptr);

        cmdList.BindPipeline(engine->ibl.lum2Pipeline, true);
        VkDescriptorBufferInfo meanLumBufferInfo{engine->ibl.lumMeanBuffer, 0, sizeof(float)};

        VkDescriptorSet vkLum2Set = ((VulkanRHI*)engine->appState->rhi)->GetVkDescriptorSet(engine->ibl.lum2DescriptorSet);
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

        vkUpdateDescriptorSets(engine->device, 2, ws2, 0, nullptr);
        struct {
            uint32_t g;
            uint32_t p;
        } pc = {nG, engine->envHdrWidth * engine->envHdrHeight};
        cmdList.PushConstants(engine->ibl.lum2PipelineLayout, ShaderStage::Compute, 0, sizeof(pc), &pc);
        cmdList.BindDescriptorSets(engine->ibl.lum2PipelineLayout, 0, 1, &engine->ibl.lum2DescriptorSet, true);
        cmdList.Dispatch(1, 1, 1);

        vk_end_label(engine->device, cb);
    }

    end_single_time_commands(engine, cb);

    void* data = nullptr;
    float meanLum = 1.0f;
    if (vmaMapMemory(engine->allocator, engine->ibl.lumMeanAllocation, &data) == VK_SUCCESS) {
        memcpy(&meanLum, data, sizeof(float));
        vmaUnmapMemory(engine->allocator, engine->ibl.lumMeanAllocation);
        if (std::isnan(meanLum) || std::isinf(meanLum) || meanLum <= 0.0f)
            meanLum = 1.0f;
        engine->ibl.bakedMeanLuminance = meanLum;
        LOG_INFO("ibl", "Mean luminance: %.4f (threshold = %.4f)", meanLum, meanLum * 3.0f);
    }

    float threshold = engine->ibl.bakedMeanLuminance * 3.0f;

    cb = begin_single_time_commands(engine);
    if (!cb)
        return;
    tracy_vk_collect(engine, cb);

    vk_begin_label(engine->device, cb, "IBL_Bake_Pass2", 0.3f, 0.6f, 0.9f);

    VulkanCommandList cmdList2((VulkanRHI*)engine->appState->rhi, cb);

    if (!engine->ibl.brdfLutBaked) {
        SVK_TRACY_VK_NAMED_ZONE(gpuIblBrdfZone, engine, cb, "GPU IBL BRDF LUT");
        cmdList2.BindPipeline(engine->ibl.brdfLutPipeline, true);

        DescriptorImageInfo iI{engine->envHdrSampler, INVALID_HANDLE, engine->envHdrImage, TextureLayout::ShaderReadOnlyOptimal};
        DescriptorImageInfo oI{INVALID_HANDLE, INVALID_HANDLE, engine->ibl.brdfLut, TextureLayout::General};

        WriteDescriptorSet ws[2] = {};
        ws[0].dstSet = engine->ibl.brdfLutDescriptorSet;
        ws[0].dstBinding = 0;
        ws[0].dstArrayElement = 0;
        ws[0].descriptorCount = 1;
        ws[0].descriptorType = DescriptorType::CombinedImageSampler;
        ws[0].pImageInfo = &iI;

        ws[1].dstSet = engine->ibl.brdfLutDescriptorSet;
        ws[1].dstBinding = 1;
        ws[1].dstArrayElement = 0;
        ws[1].descriptorCount = 1;
        ws[1].descriptorType = DescriptorType::StorageImage;
        ws[1].pImageInfo = &oI;

        engine->appState->rhi->UpdateDescriptorSets(2, ws);
        cmdList2.BindDescriptorSets(engine->ibl.iblPipelineLayout, 0, 1, &engine->ibl.brdfLutDescriptorSet, true);
        cmdList2.Dispatch(IBL_BRDF_SIZE / 32, IBL_BRDF_SIZE / 32, 1);
        engine->ibl.brdfLutBaked = true;
    }

    {
        SVK_TRACY_VK_NAMED_ZONE(gpuIblIrrZone, engine, cb, "GPU IBL Irradiance");
        LOG_INFO("ibl", "Pass 2: Irradiance Map...");
        cmdList2.BindPipeline(engine->ibl.irmapPipeline, true);
        DescriptorImageInfo iI{engine->envHdrSampler, INVALID_HANDLE, engine->envHdrImage, TextureLayout::ShaderReadOnlyOptimal};
        DescriptorImageInfo oI{INVALID_HANDLE, INVALID_HANDLE, engine->ibl.irradianceMap, TextureLayout::General};

        WriteDescriptorSet ws[2] = {};
        ws[0].dstSet = engine->ibl.irmapDescriptorSet;
        ws[0].dstBinding = 0;
        ws[0].dstArrayElement = 0;
        ws[0].descriptorCount = 1;
        ws[0].descriptorType = DescriptorType::CombinedImageSampler;
        ws[0].pImageInfo = &iI;

        ws[1].dstSet = engine->ibl.irmapDescriptorSet;
        ws[1].dstBinding = 1;
        ws[1].dstArrayElement = 0;
        ws[1].descriptorCount = 1;
        ws[1].descriptorType = DescriptorType::StorageImage;
        ws[1].pImageInfo = &oI;

        engine->appState->rhi->UpdateDescriptorSets(2, ws);
        struct {
            float t;
            int oy;
            int my;
        } pc = {threshold, 0, (int)IBL_IRM_SIZE};
        cmdList2.PushConstants(engine->ibl.iblPipelineLayout, ShaderStage::Compute, 0, sizeof(pc), &pc);
        cmdList2.BindDescriptorSets(engine->ibl.iblPipelineLayout, 0, 1, &engine->ibl.irmapDescriptorSet, true);
        cmdList2.Dispatch(IBL_IRM_SIZE / 32, IBL_IRM_SIZE / 32, 1);
    }

    std::vector<ImageViewHandle> views;
    DescriptorPoolHandle poolHandle = INVALID_HANDLE;

    {
        SVK_TRACY_VK_NAMED_ZONE(gpuIblSpecZone, engine, cb, "GPU IBL Specular");

        LOG_INFO("ibl", "Pass 2: Specular Map...");
        cmdList2.BindPipeline(engine->ibl.spmapPipeline, true);

        DescriptorPoolSize ps[2] = {{DescriptorType::CombinedImageSampler, IBL_SPM_MIPS}, {DescriptorType::StorageImage, IBL_SPM_MIPS}};
        DescriptorPoolDesc desc{ps, 2, IBL_SPM_MIPS};
        poolHandle = engine->appState->rhi->CreateDescriptorPool(desc);
        if (poolHandle != INVALID_HANDLE) {
            std::vector<DescriptorLayoutHandle> layouts(IBL_SPM_MIPS, engine->ibl.iblDescriptorSetLayout);
            DescriptorSetAllocateDesc ai{};
            ai.pool = poolHandle;
            ai.setCount = IBL_SPM_MIPS;
            ai.layouts = layouts.data();

            std::vector<DescriptorSetHandle> sets(IBL_SPM_MIPS);
            if (engine->appState->rhi->AllocateDescriptorSets(ai, sets.data())) {
                for (uint32_t i = 0; i < IBL_SPM_MIPS; ++i) {
                    ImageViewHandle viewHandle = engine->appState->rhi->CreateImageView(engine->ibl.prefilteredMap, i, 1, 0, 1);
                    views.push_back(viewHandle);

                    DescriptorImageInfo iI{engine->envHdrSampler, INVALID_HANDLE, engine->envHdrImage, TextureLayout::ShaderReadOnlyOptimal};
                    DescriptorImageInfo oI{INVALID_HANDLE, viewHandle, INVALID_HANDLE, TextureLayout::General};

                    WriteDescriptorSet ws[2] = {};
                    ws[0].dstSet = sets[i];
                    ws[0].dstBinding = 0;
                    ws[0].dstArrayElement = 0;
                    ws[0].descriptorCount = 1;
                    ws[0].descriptorType = DescriptorType::CombinedImageSampler;
                    ws[0].pImageInfo = &iI;
                    ws[1].dstSet = sets[i];
                    ws[1].dstBinding = 1;
                    ws[1].dstArrayElement = 0;
                    ws[1].descriptorCount = 1;
                    ws[1].descriptorType = DescriptorType::StorageImage;
                    ws[1].pImageInfo = &oI;

                    engine->appState->rhi->UpdateDescriptorSets(2, ws);

                    uint32_t sz = std::max(1u, IBL_SPM_SIZE >> i);
                    struct {
                        float roughness;
                        int mip;
                        float threshold;
                        int offsetY;
                        int maxY;
                    } pc = {(float)i / (float)(IBL_SPM_MIPS - 1), (int)i, threshold, 0, (int)sz};
                    cmdList2.PushConstants(engine->ibl.iblPipelineLayout, ShaderStage::Compute, 0, sizeof(pc), &pc);
                    cmdList2.BindDescriptorSets(engine->ibl.iblPipelineLayout, 0, 1, &sets[i], true);
                    cmdList2.Dispatch((sz + 31) / 32, (sz + 31) / 32, 1);
                }
            }
        }

        transition_image_layout(engine, cb, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->ibl.irradianceMap), 1, VK_IMAGE_LAYOUT_GENERAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        transition_image_layout(engine, cb, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->ibl.prefilteredMap), IBL_SPM_MIPS, VK_IMAGE_LAYOUT_GENERAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        transition_image_layout(engine, cb, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->ibl.brdfLut), 1, VK_IMAGE_LAYOUT_GENERAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }

    vk_end_label(engine->device, cb);
    end_single_time_commands(engine, cb);

    for (auto v : views)
        engine->appState->rhi->DestroyImageView(v);
    if (poolHandle != INVALID_HANDLE)
        engine->appState->rhi->DestroyDescriptorPool(poolHandle);
}

void vk_ibl_export_maps(VulkanEngine* engine) {
    const char* dump_dir = "/tmp/ibl_tests/vk";
    std::error_code ec;
    std::filesystem::create_directories(dump_dir, ec);
    if (ec) {
        LOG_ERROR("ibl", "Failed to create dump directory: %s", ec.message().c_str());
    }
    LOG_INFO("ibl", "Exporting IBL maps to HDR files in %s...", dump_dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/brdf_lut.hdr", dump_dir);
    save_image_as_hdr(engine, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->ibl.brdfLut), IBL_BRDF_SIZE, IBL_BRDF_SIZE, VK_FORMAT_R16G16_SFLOAT, 0,
                      path);
    snprintf(path, sizeof(path), "%s/irradiance.hdr", dump_dir);
    save_image_as_hdr(engine, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->ibl.irradianceMap), IBL_IRM_SIZE, IBL_IRM_SIZE,
                      VK_FORMAT_R16G16B16A16_SFLOAT, 0, path);
    for (uint32_t i = 0; i < IBL_SPM_MIPS; ++i) {
        uint32_t sz = std::max(1u, IBL_SPM_SIZE >> i);
        snprintf(path, sizeof(path), "%s/prefiltered_mip%u.hdr", dump_dir, i);
        save_image_as_hdr(engine, ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->ibl.prefilteredMap), sz, sz, VK_FORMAT_R16G16B16A16_SFLOAT, i, path);
    }
    LOG_DEBUG("ibl", "Export Complete.");
}
