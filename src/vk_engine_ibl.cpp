#include "vk_engine_ibl.h"
#include "app_log.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stb/stb_image_write.h>
#include <sys/stat.h>
#include <vector>

namespace {

VkCommandBuffer begin_single_time_commands(VulkanEngine* engine) {
    LOG_DEBUG("ibl", "TRACE: begin_single_time_commands start");
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
    LOG_DEBUG("ibl", "vkAllocateCommandBuffers start (device=%p, pool=%p)", (void*)engine->device, (void*)engine->commandPool);
    if (vkAllocateCommandBuffers(engine->device, &allocInfo, &cb) != VK_SUCCESS) {
        LOG_ERROR("ibl", "Failed to allocate command buffer");
        return VK_NULL_HANDLE;
    }
    LOG_DEBUG("ibl", "vkAllocateCommandBuffers success (cb=%p)", (void*)cb);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    LOG_DEBUG("ibl", "vkBeginCommandBuffer start");
    if (vkBeginCommandBuffer(cb, &beginInfo) != VK_SUCCESS) {
        LOG_ERROR("ibl", "Failed to begin command buffer");
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &cb);
        return VK_NULL_HANDLE;
    }
    LOG_DEBUG("ibl", "vkBeginCommandBuffer success");

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

    LOG_INFO("ibl", "save_image_as_hdr: creating staging buffer for %s (%ux%u)...", filename, width, height);
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
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readbackBuffer, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    end_single_time_commands(engine, cb);

    LOG_INFO("ibl", "save_image_as_hdr: mapping and converting data...");
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

    LOG_INFO("ibl", "save_image_as_hdr: writing %s...", filename);
    bool ok = stbi_write_hdr(filename, (int)width, (int)height, writeChannels, finalData.data());
    vmaDestroyBuffer(engine->allocator, readbackBuffer, readbackAllocation);
    return ok;
}

void transition_image_layout(VkCommandBuffer cb, VkImage image, uint32_t mipLevels, VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags srcAccess,
                             VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
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

    vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

VkShaderModule load_shader_module(VkDevice device, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        LOG_ERROR("ibl", "Failed to open shader file: %s", path);
        return VK_NULL_HANDLE;
    }
    fseek(f, 0, SEEK_END);
    size_t size = static_cast<size_t>(ftell(f));
    fseek(f, 0, SEEK_SET);
    std::vector<uint32_t> buffer(size / 4);
    if (fread(buffer.data(), 1, size, f) != size) {
        LOG_ERROR("ibl", "Failed to read shader file: %s", path);
        fclose(f);
        return VK_NULL_HANDLE;
    }
    fclose(f);

    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode = buffer.data();
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &mod) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return mod;
}

bool create_compute_pipeline(VkDevice device, VkShaderModule shaderModule, VkPipelineLayout layout, VkPipeline* pipeline, const char* name) {
    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = shaderModule;
    info.stage.pName = "main";
    info.layout = layout;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, pipeline) != VK_SUCCESS) {
        return false;
    }
    vk_set_object_name(device, (uint64_t)*pipeline, VK_OBJECT_TYPE_PIPELINE, name);
    return true;
}

} // namespace

bool init_ibl(VulkanEngine* engine) {
    LOG_INFO("ibl", "Initializing IBL module: creating images...");
    {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = {IBL_IRM_SIZE, IBL_IRM_SIZE, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.samples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateImage(engine->allocator, &info, &allocInfo, &engine->ibl.irradianceMap, &engine->ibl.irradianceMapAllocation, nullptr) != VK_SUCCESS)
            return false;
        vk_set_object_name(engine->device, (uint64_t)engine->ibl.irradianceMap, VK_OBJECT_TYPE_IMAGE, "IBL_IrradianceMap");

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = engine->ibl.irradianceMap;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = info.format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(engine->device, &viewInfo, nullptr, &engine->ibl.irradianceMapView) != VK_SUCCESS)
            return false;
    }

    {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = {IBL_SPM_SIZE, IBL_SPM_SIZE, 1};
        info.mipLevels = IBL_SPM_MIPS;
        info.arrayLayers = 1;
        info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.samples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateImage(engine->allocator, &info, &allocInfo, &engine->ibl.prefilteredMap, &engine->ibl.prefilteredMapAllocation, nullptr) != VK_SUCCESS)
            return false;
        vk_set_object_name(engine->device, (uint64_t)engine->ibl.prefilteredMap, VK_OBJECT_TYPE_IMAGE, "IBL_PrefilteredMap");

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = engine->ibl.prefilteredMap;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = info.format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, IBL_SPM_MIPS, 0, 1};
        if (vkCreateImageView(engine->device, &viewInfo, nullptr, &engine->ibl.prefilteredMapView) != VK_SUCCESS)
            return false;
    }

    {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = {IBL_BRDF_SIZE, IBL_BRDF_SIZE, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.format = VK_FORMAT_R16G16_SFLOAT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.samples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateImage(engine->allocator, &info, &allocInfo, &engine->ibl.brdfLut, &engine->ibl.brdfLutAllocation, nullptr) != VK_SUCCESS)
            return false;
        vk_set_object_name(engine->device, (uint64_t)engine->ibl.brdfLut, VK_OBJECT_TYPE_IMAGE, "IBL_BrdfLut");

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = engine->ibl.brdfLut;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = info.format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(engine->device, &viewInfo, nullptr, &engine->ibl.brdfLutView) != VK_SUCCESS)
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

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.maxLod = 12.0f;
    if (vkCreateSampler(engine->device, &samplerInfo, nullptr, &engine->ibl.irradianceSampler) != VK_SUCCESS)
        return false;
    if (vkCreateSampler(engine->device, &samplerInfo, nullptr, &engine->ibl.prefilteredSampler) != VK_SUCCESS)
        return false;
    if (vkCreateSampler(engine->device, &samplerInfo, nullptr, &engine->ibl.brdfLutSampler) != VK_SUCCESS)
        return false;

    {
        VkDescriptorSetLayoutBinding b[2] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                             {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 2, b};
        if (vkCreateDescriptorSetLayout(engine->device, &info, nullptr, &engine->ibl.iblDescriptorSetLayout) != VK_SUCCESS)
            return false;

        b[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        if (vkCreateDescriptorSetLayout(engine->device, &info, nullptr, &engine->ibl.lum1DescriptorSetLayout) != VK_SUCCESS)
            return false;

        VkDescriptorSetLayoutBinding b2[2] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                              {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        info.pBindings = b2;
        if (vkCreateDescriptorSetLayout(engine->device, &info, nullptr, &engine->ibl.lum2DescriptorSetLayout) != VK_SUCCESS)
            return false;
    }

    {
        VkPushConstantRange r{VK_SHADER_STAGE_COMPUTE_BIT, 0, 64};
        VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &engine->ibl.iblDescriptorSetLayout, 1, &r};
        if (vkCreatePipelineLayout(engine->device, &info, nullptr, &engine->ibl.iblPipelineLayout) != VK_SUCCESS)
            return false;
        info.pSetLayouts = &engine->ibl.lum1DescriptorSetLayout;
        if (vkCreatePipelineLayout(engine->device, &info, nullptr, &engine->ibl.lum1PipelineLayout) != VK_SUCCESS)
            return false;
        info.pSetLayouts = &engine->ibl.lum2DescriptorSetLayout;
        if (vkCreatePipelineLayout(engine->device, &info, nullptr, &engine->ibl.lum2PipelineLayout) != VK_SUCCESS)
            return false;
    }

    {
        VkShaderModule l1 = load_shader_module(engine->device, "shaders/ibl_lum_pass1.spv");
        VkShaderModule l2 = load_shader_module(engine->device, "shaders/ibl_lum_pass2.spv");
        VkShaderModule ir = load_shader_module(engine->device, "shaders/ibl_irmap.spv");
        VkShaderModule sp = load_shader_module(engine->device, "shaders/ibl_spmap.spv");
        VkShaderModule br = load_shader_module(engine->device, "shaders/ibl_spbrdf.spv");
        if (!l1 || !l2 || !ir || !sp || !br)
            return false;
        create_compute_pipeline(engine->device, l1, engine->ibl.lum1PipelineLayout, &engine->ibl.lum1Pipeline, "IBL_Lum1");
        create_compute_pipeline(engine->device, l2, engine->ibl.lum2PipelineLayout, &engine->ibl.lum2Pipeline, "IBL_Lum2");
        create_compute_pipeline(engine->device, ir, engine->ibl.iblPipelineLayout, &engine->ibl.irmapPipeline, "IBL_Irr");
        create_compute_pipeline(engine->device, sp, engine->ibl.iblPipelineLayout, &engine->ibl.spmapPipeline, "IBL_Spec");
        create_compute_pipeline(engine->device, br, engine->ibl.iblPipelineLayout, &engine->ibl.brdfLutPipeline, "IBL_Brdf");
        vkDestroyShaderModule(engine->device, l1, nullptr);
        vkDestroyShaderModule(engine->device, l2, nullptr);
        vkDestroyShaderModule(engine->device, ir, nullptr);
        vkDestroyShaderModule(engine->device, sp, nullptr);
        vkDestroyShaderModule(engine->device, br, nullptr);
    }

    {
        VkDescriptorPoolSize sizes[3] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 20}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 20}};
        VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 10, 3, sizes};
        if (vkCreateDescriptorPool(engine->device, &info, nullptr, &engine->ibl.computeDescriptorPool) != VK_SUCCESS)
            return false;
        VkDescriptorSetLayout layouts[5] = {engine->ibl.lum1DescriptorSetLayout, engine->ibl.lum2DescriptorSetLayout, engine->ibl.iblDescriptorSetLayout,
                                            engine->ibl.iblDescriptorSetLayout, engine->ibl.iblDescriptorSetLayout};
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, engine->ibl.computeDescriptorPool, 5, layouts};
        VkDescriptorSet sets[5];
        if (vkAllocateDescriptorSets(engine->device, &ai, sets) != VK_SUCCESS)
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
    vkDestroySampler(engine->device, engine->ibl.irradianceSampler, nullptr);
    vkDestroySampler(engine->device, engine->ibl.prefilteredSampler, nullptr);
    vkDestroySampler(engine->device, engine->ibl.brdfLutSampler, nullptr);
    vkDestroyImageView(engine->device, engine->ibl.irradianceMapView, nullptr);
    vkDestroyImageView(engine->device, engine->ibl.prefilteredMapView, nullptr);
    vkDestroyImageView(engine->device, engine->ibl.brdfLutView, nullptr);
    vmaDestroyImage(engine->allocator, engine->ibl.irradianceMap, engine->ibl.irradianceMapAllocation);
    vmaDestroyImage(engine->allocator, engine->ibl.prefilteredMap, engine->ibl.prefilteredMapAllocation);
    vmaDestroyImage(engine->allocator, engine->ibl.brdfLut, engine->ibl.brdfLutAllocation);
    vmaDestroyBuffer(engine->allocator, engine->ibl.lumGroupSumsBuffer, engine->ibl.lumGroupSumsAllocation);
    vmaDestroyBuffer(engine->allocator, engine->ibl.lumMeanBuffer, engine->ibl.lumMeanAllocation);
    vkDestroyPipeline(engine->device, engine->ibl.lum1Pipeline, nullptr);
    vkDestroyPipeline(engine->device, engine->ibl.lum2Pipeline, nullptr);
    vkDestroyPipeline(engine->device, engine->ibl.irmapPipeline, nullptr);
    vkDestroyPipeline(engine->device, engine->ibl.spmapPipeline, nullptr);
    vkDestroyPipeline(engine->device, engine->ibl.brdfLutPipeline, nullptr);
    vkDestroyPipelineLayout(engine->device, engine->ibl.iblPipelineLayout, nullptr);
    vkDestroyPipelineLayout(engine->device, engine->ibl.lum1PipelineLayout, nullptr);
    vkDestroyPipelineLayout(engine->device, engine->ibl.lum2PipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(engine->device, engine->ibl.iblDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(engine->device, engine->ibl.lum1DescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(engine->device, engine->ibl.lum2DescriptorSetLayout, nullptr);
    vkDestroyDescriptorPool(engine->device, engine->ibl.computeDescriptorPool, nullptr);
}

void vk_ibl_bake(VulkanEngine* engine) {
    if (!engine || engine->device == VK_NULL_HANDLE || engine->allocator == VK_NULL_HANDLE || engine->commandPool == VK_NULL_HANDLE) {
        LOG_WARNING("ibl", "vk_ibl_bake: engine not fully initialized, skipping bake.");
        return;
    }
    if (engine->envHdrImage == VK_NULL_HANDLE || engine->envHdrImageView == VK_NULL_HANDLE) {
        LOG_WARNING("ibl", "vk_ibl_bake: envHdr resources not ready, skipping bake.");
        return;
    }
    LOG_INFO("ibl", "Baking IBL maps for %ux%u environment map...", engine->envHdrWidth, engine->envHdrHeight);

    LOG_DEBUG("ibl", "TRACE: Pass 1 start");
    VkCommandBuffer cb = begin_single_time_commands(engine);
    if (!cb)
        return;
    LOG_DEBUG("ibl", "TRACE: Pass 1 cb ok");
    vk_begin_label(engine->device, cb, "IBL_Bake_Pass1", 0.5f, 0.2f, 0.8f);

    transition_image_layout(cb, engine->ibl.irradianceMap, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    transition_image_layout(cb, engine->ibl.prefilteredMap, IBL_SPM_MIPS, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    if (!engine->ibl.brdfLutBaked) {
        transition_image_layout(cb, engine->ibl.brdfLut, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT,
                                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    uint32_t dX = (engine->envHdrWidth + 15) / 16;
    uint32_t dY = (engine->envHdrHeight + 15) / 16;
    uint32_t nG = std::min(dX * dY, IBL_MAX_GROUPS);

    {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, engine->ibl.lum1Pipeline);
        VkDescriptorImageInfo iI{engine->envHdrSampler, engine->envHdrImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo bI{engine->ibl.lumGroupSumsBuffer, 0, nG * sizeof(float)};

        VkWriteDescriptorSet ws[2] = {};
        ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[0].dstSet = engine->ibl.lum1DescriptorSet;
        ws[0].dstBinding = 0;
        ws[0].descriptorCount = 1;
        ws[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ws[0].pImageInfo = &iI;

        ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[1].dstSet = engine->ibl.lum1DescriptorSet;
        ws[1].dstBinding = 1;
        ws[1].descriptorCount = 1;
        ws[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ws[1].pBufferInfo = &bI;

        vkUpdateDescriptorSets(engine->device, 2, ws, 0, nullptr);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, engine->ibl.lum1PipelineLayout, 0, 1, &engine->ibl.lum1DescriptorSet, 0, nullptr);
        vkCmdDispatch(cb, dX, dY, 1);

        VkBufferMemoryBarrier bar{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                  nullptr,
                                  VK_ACCESS_SHADER_WRITE_BIT,
                                  VK_ACCESS_SHADER_READ_BIT,
                                  VK_QUEUE_FAMILY_IGNORED,
                                  VK_QUEUE_FAMILY_IGNORED,
                                  engine->ibl.lumGroupSumsBuffer,
                                  0,
                                  nG * sizeof(float)};
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &bar, 0, nullptr);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, engine->ibl.lum2Pipeline);
        VkDescriptorBufferInfo rI{engine->ibl.lumMeanBuffer, 0, sizeof(float)};

        VkWriteDescriptorSet ws2[2] = {};
        ws2[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws2[0].dstSet = engine->ibl.lum2DescriptorSet;
        ws2[0].dstBinding = 0;
        ws2[0].descriptorCount = 1;
        ws2[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ws2[0].pBufferInfo = &bI;

        ws2[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws2[1].dstSet = engine->ibl.lum2DescriptorSet;
        ws2[1].dstBinding = 1;
        ws2[1].descriptorCount = 1;
        ws2[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ws2[1].pBufferInfo = &rI;

        vkUpdateDescriptorSets(engine->device, 2, ws2, 0, nullptr);
        struct {
            uint32_t g;
            uint32_t p;
        } pc = {nG, engine->envHdrWidth * engine->envHdrHeight};
        vkCmdPushConstants(cb, engine->ibl.lum2PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, engine->ibl.lum2PipelineLayout, 0, 1, &engine->ibl.lum2DescriptorSet, 0, nullptr);
        vkCmdDispatch(cb, 1, 1, 1);
    }

    vk_end_label(engine->device, cb);
    end_single_time_commands(engine, cb);

    void* data = nullptr;
    if (vmaMapMemory(engine->allocator, engine->ibl.lumMeanAllocation, &data) == VK_SUCCESS) {
        float meanLum = 1.0f;
        memcpy(&meanLum, data, sizeof(float));
        vmaUnmapMemory(engine->allocator, engine->ibl.lumMeanAllocation);
        if (std::isnan(meanLum) || std::isinf(meanLum) || meanLum <= 0.0f)
            meanLum = 1.0f;
        engine->ibl.bakedMeanLuminance = meanLum;
        LOG_INFO("ibl", "Mean luminance: %.4f (threshold = %.4f)", meanLum, meanLum * 3.0f);
    }

    LOG_DEBUG("ibl", "TRACE: Pass 2 start");
    cb = begin_single_time_commands(engine);
    if (!cb)
        return;
    LOG_DEBUG("ibl", "TRACE: Pass 2 cb ok");
    float threshold = engine->ibl.bakedMeanLuminance * 3.0f;
    if (!engine->ibl.brdfLutBaked) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, engine->ibl.brdfLutPipeline);
        VkDescriptorImageInfo oI{VK_NULL_HANDLE, engine->ibl.brdfLutView, VK_IMAGE_LAYOUT_GENERAL};

        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = engine->ibl.brdfLutDescriptorSet;
        w.dstBinding = 1;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w.pImageInfo = &oI;

        vkUpdateDescriptorSets(engine->device, 1, &w, 0, nullptr);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, engine->ibl.iblPipelineLayout, 0, 1, &engine->ibl.brdfLutDescriptorSet, 0, nullptr);
        LOG_DEBUG("ibl", "TRACE: Pass 2 BRDF dispatch");
        vkCmdDispatch(cb, IBL_BRDF_SIZE / 32, IBL_BRDF_SIZE / 32, 1);
        engine->ibl.brdfLutBaked = true;
    }

    {
        LOG_INFO("ibl", "Pass 2: Irradiance Map...");
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, engine->ibl.irmapPipeline);
        VkDescriptorImageInfo iI{engine->envHdrSampler, engine->envHdrImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo oI{VK_NULL_HANDLE, engine->ibl.irradianceMapView, VK_IMAGE_LAYOUT_GENERAL};

        VkWriteDescriptorSet ws[2] = {};
        ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[0].dstSet = engine->ibl.irmapDescriptorSet;
        ws[0].dstBinding = 0;
        ws[0].descriptorCount = 1;
        ws[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ws[0].pImageInfo = &iI;

        ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[1].dstSet = engine->ibl.irmapDescriptorSet;
        ws[1].dstBinding = 1;
        ws[1].descriptorCount = 1;
        ws[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        ws[1].pImageInfo = &oI;

        vkUpdateDescriptorSets(engine->device, 2, ws, 0, nullptr);
        struct {
            float t;
            int oy;
            int my;
        } pc = {threshold, 0, (int)IBL_IRM_SIZE};
        vkCmdPushConstants(cb, engine->ibl.iblPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, engine->ibl.iblPipelineLayout, 0, 1, &engine->ibl.irmapDescriptorSet, 0, nullptr);
        LOG_DEBUG("ibl", "TRACE: Pass 2 IRMAP dispatch");
        vkCmdDispatch(cb, IBL_IRM_SIZE / 32, IBL_IRM_SIZE / 32, 1);
    }

    {
        LOG_INFO("ibl", "Pass 2: Specular Map...");
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, engine->ibl.spmapPipeline);
        std::vector<VkImageView> views;
        VkDescriptorPoolSize ps[3] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IBL_SPM_MIPS},
                                      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, IBL_SPM_MIPS},
                                      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, IBL_SPM_MIPS}};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, IBL_SPM_MIPS, 3, ps};
        VkDescriptorPool pool;
        if (vkCreateDescriptorPool(engine->device, &pi, nullptr, &pool) != VK_SUCCESS) {
            LOG_ERROR("ibl", "Failed to create temporary descriptor pool for Specular Map");
            return;
        }
        std::vector<VkDescriptorSetLayout> l(IBL_SPM_MIPS, engine->ibl.iblDescriptorSetLayout);
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, pool, IBL_SPM_MIPS, l.data()};
        std::vector<VkDescriptorSet> sets(IBL_SPM_MIPS);
        if (vkAllocateDescriptorSets(engine->device, &ai, sets.data()) != VK_SUCCESS) {
            LOG_ERROR("ibl", "Failed to allocate descriptor sets for Specular Map");
            vkDestroyDescriptorPool(engine->device, pool, nullptr);
            return;
        }

        for (uint32_t i = 0; i < IBL_SPM_MIPS; ++i) {
            uint32_t sz = std::max(1u, IBL_SPM_SIZE >> i);
            VkImageView v;
            VkImageViewCreateInfo vi{
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, engine->ibl.prefilteredMap, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {},
                {VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, 1}};
            vkCreateImageView(engine->device, &vi, nullptr, &v);
            views.push_back(v);
            VkDescriptorImageInfo iI{engine->envHdrSampler, engine->envHdrImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkDescriptorImageInfo oI{VK_NULL_HANDLE, v, VK_IMAGE_LAYOUT_GENERAL};

            VkWriteDescriptorSet ws[2] = {};
            ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            ws[0].dstSet = sets[i];
            ws[0].dstBinding = 0;
            ws[0].descriptorCount = 1;
            ws[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            ws[0].pImageInfo = &iI;

            ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            ws[1].dstSet = sets[i];
            ws[1].dstBinding = 1;
            ws[1].descriptorCount = 1;
            ws[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            ws[1].pImageInfo = &oI;

            vkUpdateDescriptorSets(engine->device, 2, ws, 0, nullptr);
            struct {
                float r;
                int m;
                float t;
                int oy;
                int my;
            } sp = {(float)i / (float)(IBL_SPM_MIPS - 1), (int)i, threshold, 0, (int)sz};
            vkCmdPushConstants(cb, engine->ibl.iblPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(sp), &sp);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, engine->ibl.iblPipelineLayout, 0, 1, &sets[i], 0, nullptr);
            vkCmdDispatch(cb, (sz + 31) / 32, (sz + 31) / 32, 1);
        }
        transition_image_layout(cb, engine->ibl.irradianceMap, 1, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
                                VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        transition_image_layout(cb, engine->ibl.prefilteredMap, IBL_SPM_MIPS, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        transition_image_layout(cb, engine->ibl.brdfLut, 1, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
                                VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        vk_end_label(engine->device, cb);
        end_single_time_commands(engine, cb);
        LOG_DEBUG("ibl", "Pass 2 End");
        for (auto* v : views)
            vkDestroyImageView(engine->device, v, nullptr);
        vkDestroyDescriptorPool(engine->device, pool, nullptr);
    }

    const char* dumpEnv = std::getenv("SVK_IBL_DUMP");
    if (dumpEnv && dumpEnv[0] != '\0')
        vk_ibl_export_maps(engine);
}

void vk_ibl_export_maps(VulkanEngine* engine) {
    LOG_DEBUG("ibl", "Export Start");
    const char* dump_dir = "/tmp/ibl_tests/vk";
    const mode_t mode = 0755;
    if (mkdir(dump_dir, mode) == -1 && errno != EEXIST) {
        LOG_ERROR("ibl", "Failed to create dump directory");
    }
    LOG_INFO("ibl", "Exporting IBL maps to HDR files in %s...", dump_dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/brdf_lut.hdr", dump_dir);
    save_image_as_hdr(engine, engine->ibl.brdfLut, IBL_BRDF_SIZE, IBL_BRDF_SIZE, VK_FORMAT_R16G16_SFLOAT, 0, path);
    snprintf(path, sizeof(path), "%s/irradiance.hdr", dump_dir);
    save_image_as_hdr(engine, engine->ibl.irradianceMap, IBL_IRM_SIZE, IBL_IRM_SIZE, VK_FORMAT_R16G16B16A16_SFLOAT, 0, path);
    for (uint32_t i = 0; i < IBL_SPM_MIPS; ++i) {
        uint32_t sz = std::max(1u, IBL_SPM_SIZE >> i);
        snprintf(path, sizeof(path), "%s/prefiltered_mip%u.hdr", dump_dir, i);
        save_image_as_hdr(engine, engine->ibl.prefilteredMap, sz, sz, VK_FORMAT_R16G16B16A16_SFLOAT, i, path);
    }
    LOG_DEBUG("ibl", "Export Complete. Exiting.");
    exit(0);
}
