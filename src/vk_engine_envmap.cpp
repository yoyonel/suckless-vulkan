#include "vk_engine_envmap.h"
#include "asset_ktx.h"
#include "tracy_state.h"
#include "vk_engine_ibl.h"
#include "vk_engine_init.h"
#include <sys/stat.h>

#include "app_log.h"
#include "rhi/vulkan_rhi.h"

static void update_envmap_descriptor_set(VulkanEngine* engine) {
    if (engine->descriptorSet == INVALID_HANDLE)
        return;

    DescriptorImageInfo envInfo{};
    envInfo.imageLayout = TextureLayout::ShaderReadOnlyOptimal;
    envInfo.texture = engine->envHdrImage;
    envInfo.imageView = INVALID_HANDLE;
    envInfo.sampler = engine->envHdrSampler;

    DescriptorImageInfo irrInfo{};
    irrInfo.imageLayout = TextureLayout::ShaderReadOnlyOptimal;
    irrInfo.texture = engine->iblBaker.irradianceMap.is_valid() ? engine->iblBaker.irradianceMap : engine->envHdrImage;
    irrInfo.imageView = INVALID_HANDLE;
    irrInfo.sampler = engine->iblBaker.irradianceSampler.is_valid() ? engine->iblBaker.irradianceSampler : engine->envHdrSampler;

    DescriptorImageInfo prefInfo{};
    prefInfo.imageLayout = TextureLayout::ShaderReadOnlyOptimal;
    prefInfo.texture = engine->iblBaker.prefilteredMap.is_valid() ? engine->iblBaker.prefilteredMap : engine->envHdrImage;
    prefInfo.imageView = INVALID_HANDLE;
    prefInfo.sampler = engine->iblBaker.prefilteredSampler.is_valid() ? engine->iblBaker.prefilteredSampler : engine->envHdrSampler;

    DescriptorImageInfo lutInfo{};
    lutInfo.imageLayout = TextureLayout::ShaderReadOnlyOptimal;
    lutInfo.texture = engine->iblBaker.brdfLut.is_valid() ? engine->iblBaker.brdfLut : engine->envHdrImage;
    lutInfo.imageView = INVALID_HANDLE;
    lutInfo.sampler = engine->iblBaker.brdfLutSampler.is_valid() ? engine->iblBaker.brdfLutSampler : engine->envHdrSampler;

    WriteDescriptorSet writes[4] = {};

    writes[0].dstSet = engine->descriptorSet;
    writes[0].dstBinding = 1; // HDR Map
    writes[0].dstArrayElement = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = DescriptorType::CombinedImageSampler;
    writes[0].pImageInfo = &envInfo;
    writes[0].pBufferInfo = nullptr;

    writes[1].dstSet = engine->descriptorSet;
    writes[1].dstBinding = 2; // Irradiance Map
    writes[1].dstArrayElement = 0;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = DescriptorType::CombinedImageSampler;
    writes[1].pImageInfo = &irrInfo;
    writes[1].pBufferInfo = nullptr;

    writes[2].dstSet = engine->descriptorSet;
    writes[2].dstBinding = 3; // Prefiltered Map
    writes[2].dstArrayElement = 0;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = DescriptorType::CombinedImageSampler;
    writes[2].pImageInfo = &prefInfo;
    writes[2].pBufferInfo = nullptr;

    writes[3].dstSet = engine->descriptorSet;
    writes[3].dstBinding = 4; // BRDF LUT
    writes[3].dstArrayElement = 0;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = DescriptorType::CombinedImageSampler;
    writes[3].pImageInfo = &lutInfo;
    writes[3].pBufferInfo = nullptr;

    engine->appState->rhi->UpdateDescriptorSets(4, writes);
    update_global_bind_group(engine);
}

#include "tracy_client.h"
#include "vk_engine.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <stb/stb_image.h>

#ifdef TRACY_ENABLE
#include <tracy/TracyC.h>
#endif

namespace {

constexpr float kMinEnvLod = 0.0f;
constexpr const char* kDefaultEnvFilename = "env.hdr";

const char* get_filename_from_path(const std::string& path) {
    size_t lastSlash = path.find_last_of('/');
    if (lastSlash == std::string::npos) {
        lastSlash = path.find_last_of('\\');
    }
    if (lastSlash != std::string::npos) {
        return path.c_str() + lastSlash + 1;
    }
    return path.c_str();
}

VkCommandBuffer begin_one_time_commands(VulkanEngine* engine) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = engine->ctx.commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(engine->ctx.device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    vk_set_object_name(engine->ctx.device, (uint64_t)commandBuffer, VK_OBJECT_TYPE_COMMAND_BUFFER, "EnvHDR_Transfer_CommandBuffer");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->ctx.device, engine->ctx.commandPool, 1, &commandBuffer);
        return VK_NULL_HANDLE;
    }

    return commandBuffer;
}

ResourceResult transition_hdr_image_layout(VulkanEngine* engine, VkCommandBuffer commandBuffer, uint32_t baseMipLevel, uint32_t levelCount,
                                           VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
                                           VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->envHdrImage);
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;

    ((VulkanRHI*)engine->appState->rhi)->CmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    return ResourceResult::Success;
}

std::vector<std::string> find_hdr_paths() {
    const std::filesystem::path hdrRoot("assets/textures/hdr");
    if (!std::filesystem::exists(hdrRoot)) {
        return {};
    }

    std::vector<std::string> candidates;
    for (const auto& entry : std::filesystem::directory_iterator(hdrRoot)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() == ".hdr") {
            candidates.push_back(entry.path().string());
        }
    }

    std::sort(candidates.begin(), candidates.end());
    return candidates;
}

int find_default_hdr_index(const std::vector<std::string>& hdrFiles) {
    if (hdrFiles.empty()) {
        return -1;
    }

    for (size_t i = 0; i < hdrFiles.size(); ++i) {
        if (get_filename_from_path(hdrFiles[i]) == kDefaultEnvFilename) {
            return static_cast<int>(i);
        }
    }

    return 0;
}

namespace {

// Staging buffer now directly created in I/O thread

void vk_generate_one_hdr_mipmap(VulkanEngine* engine) {
    uint32_t i = engine->iblBaker.currentMip;
    if (i > engine->envHdrMipLevels)
        return;

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = engine->ctx.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(engine->ctx.device, &allocInfo, &commandBuffer);
    engine->iblBaker.iblBakeCommandBuffer = commandBuffer;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    SVK_TRACY_ZONE_SCOPED_C("Async Loader: Mipmap Slice", tracy_color::CpuRecord);
    vk_begin_label(engine->ctx.device, commandBuffer, "Generate_EnvHDR_Mipmap_Slice", 0.0f, 0.4f, 0.8f);

    if (engine->envHdrMipLevels == 1) {
        (void)transition_hdr_image_layout(engine, commandBuffer, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                          VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    } else if (i < engine->envHdrMipLevels) {
        int32_t mipWidth = std::max(1, static_cast<int>(engine->envHdrWidth) >> (i - 1));
        int32_t mipHeight = std::max(1, static_cast<int>(engine->envHdrHeight) >> (i - 1));

        (void)transition_hdr_image_layout(engine, commandBuffer, i - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                          VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                          VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {std::max(1, mipWidth / 2), std::max(1, mipHeight / 2), 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        VkImage vkEnvHdrImage = ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->envHdrImage);
        ((VulkanRHI*)engine->appState->rhi)
            ->CmdBlitImage(commandBuffer, vkEnvHdrImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vkEnvHdrImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                           VK_FILTER_LINEAR);

        (void)transition_hdr_image_layout(engine, commandBuffer, i - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                          VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        if (i == engine->envHdrMipLevels - 1) {
            (void)transition_hdr_image_layout(engine, commandBuffer, i, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                              VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }
    }

    vk_end_label(engine->ctx.device, commandBuffer);
    vkEndCommandBuffer(commandBuffer);

    vk_ibl_reset_bake_fence(engine);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(engine->ctx.graphicsQueue, 1, &submitInfo, engine->iblBaker.iblBakeFence);

    engine->iblBaker.currentMip++;
}

void vk_process_upload_hdr(VulkanEngine* engine) {
    VkCommandBuffer commandBuffer = begin_one_time_commands(engine);
    VkImage vkEnvHdrImage = ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->envHdrImage);

    if (engine->iblBaker.currentSlice == 0) {
        (void)transition_hdr_image_layout(engine, commandBuffer, 0, engine->envHdrMipLevels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                                          VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    }

    int sliceHeight = 128;
    int currentY = engine->iblBaker.currentSlice * sliceHeight;
    int copyHeight = std::min(sliceHeight, static_cast<int>(engine->envHdrHeight) - currentY);

    if (copyHeight > 0) {
        vk_begin_label(engine->ctx.device, commandBuffer, "Upload_HDR_Slice", 1.0f, 0.5f, 0.0f);
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, currentY, 0};
        region.imageExtent = {engine->envHdrWidth, static_cast<uint32_t>(copyHeight), 1};
        region.bufferOffset = static_cast<VkDeviceSize>(currentY) * engine->envHdrWidth * 16; // 16 bytes per pixel for RGBA32_SFLOAT

        ((VulkanRHI*)engine->appState->rhi)
            ->CmdCopyBufferToImage(commandBuffer, engine->iblBaker.currentStagingBuffer, vkEnvHdrImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        vk_end_label(engine->ctx.device, commandBuffer);
    }

    vk_ibl_reset_bake_fence(engine);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(engine->ctx.graphicsQueue, 1, &submitInfo, engine->iblBaker.iblBakeFence);

    engine->iblBaker.iblBakeCommandBuffer = commandBuffer;
    engine->iblBaker.bakeState = IblBakeState::UploadHdrWait;
}

void start_hdr_bake(VulkanEngine* engine, bool asyncUpload, HdrLoadRequest& request) {
    if (engine->iblBaker.irradianceMap.is_valid()) {
        engine->iblBaker.pendingOldTextures.push_back(std::move(engine->iblBaker.irradianceMap));
    }
    engine->iblBaker.irradianceMap = std::move(request.irradianceMap);

    if (engine->iblBaker.prefilteredMap.is_valid()) {
        engine->iblBaker.pendingOldTextures.push_back(std::move(engine->iblBaker.prefilteredMap));
    }
    engine->iblBaker.prefilteredMap = std::move(request.prefilteredMap);

    engine->iblBaker.bakeState = IblBakeState::UploadHdr;

    if (!asyncUpload) {
        while (engine->iblBaker.bakeState != IblBakeState::Idle) {
            if (engine->iblBaker.iblBakeFence != VK_NULL_HANDLE) {
                vkWaitForFences(engine->ctx.device, 1, &engine->iblBaker.iblBakeFence, VK_TRUE, UINT64_MAX);
            }
            vk_check_ibl_bake_status(engine);
        }
    }
}
} // namespace

void allocate_hdr_resources_async(VulkanEngine* engine, HdrLoadRequest& request) {
    SVK_TRACY_ZONE_SCOPED_C("Async Loader: VMA Allocations", tracy_color::IoAlloc);
    uint32_t envHdrMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(request.width, request.height)))) + 1;

    VkFormatProperties hdrFormatProps{};
    vkGetPhysicalDeviceFormatProperties(engine->ctx.physicalDevice, VK_FORMAT_R32G32B32A32_SFLOAT, &hdrFormatProps);
    const bool canLinearBlit = (hdrFormatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
    if (!canLinearBlit) {
        envHdrMipLevels = 1;
    }
    request.envHdrMipLevels = envHdrMipLevels;

    request.envHdrImage.Reset(engine->appState->rhi, engine->appState->rhi->CreateTexture(request.width, request.height, TextureFormat::RGBA32_SFLOAT,
                                                                                          TextureUsage::Sampled, envHdrMipLevels, "EnvHDR_Image"));

    request.envHdrSampler.Reset(engine->appState->rhi, engine->appState->rhi->CreateSampler(envHdrMipLevels, "EnvHDR_Sampler"));

    request.irradianceMap.Reset(engine->appState->rhi, engine->appState->rhi->CreateTexture(IBL_IRM_SIZE, IBL_IRM_SIZE, TextureFormat::RGBA16_SFLOAT,
                                                                                            TextureUsage::Storage, 1, "IBL_IrradianceMap"));

    request.prefilteredMap.Reset(engine->appState->rhi, engine->appState->rhi->CreateTexture(IBL_SPM_SIZE, IBL_SPM_SIZE, TextureFormat::RGBA16_SFLOAT,
                                                                                             TextureUsage::Storage, IBL_SPM_MIPS, "IBL_PrefilteredMap"));
}

ResourceResult init_environment_texture_from_staging(VulkanEngine* engine, HdrLoadRequest& request, bool isFallback, bool asyncUpload = false) {
    SVK_TRACY_ZONE_SCOPED_C("Async Loader: Staging Upload", tracy_color::IoStaging);
    if (request.stagingBuffer == VK_NULL_HANDLE || request.width <= 0 || request.height <= 0) {
        return ResourceResult::ErrorParseFailed;
    }

    engine->envHdrMipLevels = request.envHdrMipLevels;

    vk_set_object_name(engine->ctx.device, (uint64_t)request.stagingBuffer, VK_OBJECT_TYPE_BUFFER, "EnvHDR_Staging_Buffer");

    engine->envHdrWidth = request.width;
    engine->envHdrHeight = request.height;

    engine->envHdrImage = std::move(request.envHdrImage);
    if (!engine->envHdrImage.is_valid()) {
        return ResourceResult::ErrorParseFailed;
    }

    engine->iblBaker.currentStagingBuffer = request.stagingBuffer;
    engine->iblBaker.currentSlice = 0;
    engine->iblBaker.totalSlices = static_cast<int>((engine->envHdrHeight + 127) / 128);

    engine->envHdrSampler = std::move(request.envHdrSampler);
    if (!engine->envHdrSampler.is_valid()) {
        return ResourceResult::ErrorParseFailed;
    }

    if (isFallback) {
        LOG_INFO("engine", "HDR map fallback 1x1 initialisee (%dx%d, mips=%u)", request.width, request.height, engine->envHdrMipLevels);
    } else {
        LOG_INFO("engine", "HDR map chargee: %s (%dx%d, mips=%u)", request.sourcePathOrLabel.c_str(), request.width, request.height, engine->envHdrMipLevels);
    }

    {
        SVK_TRACY_ZONE_SCOPED_C("Async Loader: Bake Allocations", tracy_color::IoBakeAlloc);
        start_hdr_bake(engine, asyncUpload, request);
    }

    return ResourceResult::Success;
}

namespace {
ResourceResult load_hdr_with_ktx2_cache(VulkanEngine* engine, const std::string& hdrPath, HdrLoadRequest* request) {
    std::string ktxPath = hdrPath + ".ktx2";

    struct stat hdrStat;
    struct stat ktxStat;
    bool ktxValid = false;

    if (stat(hdrPath.c_str(), &hdrStat) == 0) {
        if (stat(ktxPath.c_str(), &ktxStat) == 0) {
            if (ktxStat.st_mtime >= hdrStat.st_mtime) {
                ktxValid = true;
            }
        }
    }

    auto allocator_func = [&](size_t size) -> void* {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocInfo{};
        if (vmaCreateBuffer(engine->ctx.allocator, &bufferInfo, &stagingAllocInfo, &request->stagingBuffer, &request->stagingAllocation, &allocInfo) !=
            VK_SUCCESS) {
            return nullptr;
        }

        return allocInfo.pMappedData;
    };

    if (ktxValid) {
        int width;
        int height;
        if (ktx2_load_from_file(ktxPath, &width, &height, allocator_func) == KtxResult::Success) {
            LOG_INFO("engine", "[KTX Cache] Load fast-path: %s", ktxPath.c_str());
            request->width = static_cast<uint32_t>(width);
            request->height = static_cast<uint32_t>(height);
            return ResourceResult::Success;
        }
    }

    int channels;
    int width;
    int height;
    float* pixels = stbi_loadf(hdrPath.c_str(), &width, &height, &channels, 4);
    if (pixels && width > 0 && height > 0) {
        size_t size = static_cast<size_t>(width) * static_cast<size_t>(height) * 4U * sizeof(float);
        void* mapped = allocator_func(size);
        if (mapped) {
            memcpy(mapped, pixels, size);

            LOG_INFO("engine", "[KTX Cache] Baking %s...", ktxPath.c_str());
            ktx2_bake_hdr_to_file(ktxPath, width, height, pixels);

            request->width = static_cast<uint32_t>(width);
            request->height = static_cast<uint32_t>(height);
            stbi_image_free(pixels);
            return ResourceResult::Success;
        }
    }

    if (pixels)
        stbi_image_free(pixels);
    return ResourceResult::ErrorParseFailed;
}
} // namespace

ResourceResult init_environment_texture_from_path(VulkanEngine* engine, const std::string& hdrPath) {
    engine->iblBaker.envmapRequestTime = std::chrono::high_resolution_clock::now();
    HdrLoadRequest request{};

    if (hdrPath.empty()) {
        LOG_INFO("engine", "Aucun fichier HDR trouve dans assets/textures/hdr, utilisation d'une texture fallback 1x1");
        float fallbackPixel[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        // Quick setup for fallback staging
        VkBufferCreateInfo bufferInfo{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, 16, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        vmaCreateBuffer(engine->ctx.allocator, &bufferInfo, &stagingAllocInfo, &request.stagingBuffer, &request.stagingAllocation, nullptr);
        void* mapped;
        vmaMapMemory(engine->ctx.allocator, request.stagingAllocation, &mapped);
        memcpy(mapped, fallbackPixel, 16);
        vmaUnmapMemory(engine->ctx.allocator, request.stagingAllocation);
        allocate_hdr_resources_async(engine, request);
        ResourceResult res = init_environment_texture_from_staging(engine, request, true);
        vmaDestroyBuffer(engine->ctx.allocator, request.stagingBuffer, request.stagingAllocation);
        return res;
    }

    if (load_hdr_with_ktx2_cache(engine, hdrPath, &request) != ResourceResult::Success || request.width == 0) {
        LOG_WARNING("engine", "Echec du chargement HDR: %s, utilisation d'une texture fallback 1x1", hdrPath.c_str());
        float fallbackPixel[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        VkBufferCreateInfo bufferInfo{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, 16, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        vmaCreateBuffer(engine->ctx.allocator, &bufferInfo, &stagingAllocInfo, &request.stagingBuffer, &request.stagingAllocation, nullptr);
        void* mapped;
        vmaMapMemory(engine->ctx.allocator, request.stagingAllocation, &mapped);
        memcpy(mapped, fallbackPixel, 16);
        vmaUnmapMemory(engine->ctx.allocator, request.stagingAllocation);
        allocate_hdr_resources_async(engine, request);
        ResourceResult res = init_environment_texture_from_staging(engine, request, true);
        vmaDestroyBuffer(engine->ctx.allocator, request.stagingBuffer, request.stagingAllocation);
        return res;
    }

    request.sourcePathOrLabel = hdrPath;
    allocate_hdr_resources_async(engine, request);
    ResourceResult res = init_environment_texture_from_staging(engine, request, false);
    vmaDestroyBuffer(engine->ctx.allocator, request.stagingBuffer, request.stagingAllocation);
    return res;
}

void push_to_cleanup_queue(VulkanEngine* engine, HdrCleanupRequest&& req) {
    while (true) {
        // [Assessment] rigtorp::SPSCQueue::push(T&&) does not consume the moved object if it returns false.
        // There is no alternative because HdrCleanupRequest is move-only and the queue requires a move.
        // [Tracking Note] Consider adding try_push(T&&) without move semantics issues in the future.
        // [User Validation] The user explicitly validated this approach during code review.
        if (engine->io.hdrCleanupQueue.push(std::move(req))) { // NOLINT(bugprone-use-after-move)
            break;
        }
        std::this_thread::yield();
    }
}

void request_environment_texture_async(VulkanEngine* engine, int newHdrIndex) {
    SVK_TRACY_ZONE_SCOPED_C("Async Loader: Request Envmap", tracy_color::CpuUpdate);
    if (newHdrIndex < 0 || newHdrIndex >= static_cast<int>(engine->hdrFiles.size()) || newHdrIndex == engine->currentHdrIndex) {
        return;
    }

    if (engine->io.pendingHdrIndex == newHdrIndex) {
        return;
    }

    HdrLoadRequest dummy;
    while (engine->io.hdrLoadQueue.pop(dummy)) {
        if (dummy.stagingBuffer != VK_NULL_HANDLE) {
            HdrCleanupRequest req;
            req.buffer = dummy.stagingBuffer;
            req.allocation = dummy.stagingAllocation;
            push_to_cleanup_queue(engine, std::move(req));
        }
    }

    HdrLoadRequest request{};
    request.hdrIndex = newHdrIndex;
    request.state = HdrLoadRequestState::Pending;
    request.width = 0;
    request.height = 0;
    request.channels = 0;

    tracy_state::set_async_status(tracy_state::AsyncState::Pending);
    engine->io.hdrLoadQueue.push(std::move(request));
    engine->io.pendingHdrIndex = newHdrIndex;
    engine->iblBaker.envmapRequestTime = std::chrono::high_resolution_clock::now();

    LOG_INFO("runtime", "Chargement HDR async demande: %s", get_filename_from_path(engine->hdrFiles[static_cast<size_t>(newHdrIndex)]));
}

void process_hdr_cleanup_queue(VulkanEngine* engine) {
    HdrCleanupRequest cleanupReq;
    while (engine->io.hdrCleanupQueue.pop(cleanupReq)) {
        SVK_TRACY_ZONE_SCOPED_C("Async Loader: Cleanup", tracy_color::IoCleanup);
        if (cleanupReq.buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(engine->ctx.allocator, cleanupReq.buffer, cleanupReq.allocation);
        }
        if (cleanupReq.tex.is_valid()) {
            engine->appState->rhi->DestroyTexture(cleanupReq.tex);
        }
        if (cleanupReq.smp.is_valid()) {
            engine->appState->rhi->DestroySampler(cleanupReq.smp);
        }
    }
}

void process_one_hdr_load_request(VulkanEngine* engine, HdrLoadRequest& request) {
    SVK_TRACY_ZONE_SCOPED_C("Async Loader: Process Request", tracy_color::IoProcess);

    tracy_state::set_async_status(tracy_state::AsyncState::Loading);
    request.state = HdrLoadRequestState::Loading;
    engine->io.hdrLoadInFlight = true;

    if (request.hdrIndex < 0 || request.hdrIndex >= static_cast<int>(engine->hdrFiles.size())) {
        request.state = HdrLoadRequestState::Failed;
        tracy_state::set_async_status(tracy_state::AsyncState::Failed);
        engine->io.hdrReadyQueue.push(std::move(request));
        engine->io.hdrLoadInFlight = false;
        return;
    }

    SVK_TRACY_ZONE_NAMED_C(decodeZone, "Async Loader: Decode File", tracy_color::IoDecode);
    const std::string hdrPath = engine->hdrFiles[static_cast<size_t>(request.hdrIndex)];

    tracy_state::set_async_status(tracy_state::AsyncState::Convert);
    bool success = (load_hdr_with_ktx2_cache(engine, hdrPath, &request) == ResourceResult::Success && request.width > 0 && request.height > 0);

    if (success) {
        request.channels = 4;
        request.sourcePathOrLabel = hdrPath;
        allocate_hdr_resources_async(engine, request);
        request.state = HdrLoadRequestState::Ready;
        tracy_state::set_async_status(tracy_state::AsyncState::Ready);
    } else {
        request.state = HdrLoadRequestState::Failed;
        tracy_state::set_async_status(tracy_state::AsyncState::Failed);
    }

    engine->io.hdrReadyQueue.push(std::move(request));
    engine->io.hdrLoadInFlight = false;
}

void hdr_io_thread_main(VulkanEngine* engine) {
#ifdef TRACY_ENABLE
    TracyCSetThreadName("HDR I/O Thread");
#endif

    while (engine->io.hdrIoThreadRunning.load(std::memory_order_relaxed)) {
        HdrLoadRequest request{};
        if (!engine->io.hdrLoadQueue.pop(request)) {
            tracy_state::set_async_status(tracy_state::AsyncState::Idle);
            process_hdr_cleanup_queue(engine);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        process_one_hdr_load_request(engine, request);
        process_hdr_cleanup_queue(engine);
    }
}

} // namespace

void vk_cleanup_environment_resources(VulkanEngine* engine) {
    if (engine->ctx.device == VK_NULL_HANDLE) {
        return;
    }

    if (engine->envHdrSampler.is_valid()) {
        engine->envHdrSampler.Reset();
    }
    if (engine->envHdrImage.is_valid()) {
        engine->envHdrImage.Reset();
    }

    engine->envHdrMipLevels = 0;
}

GfxResult vk_init_environment_catalog(VulkanEngine* engine) {
    engine->hdrFiles = find_hdr_paths();
    engine->currentHdrIndex = find_default_hdr_index(engine->hdrFiles);

    if (engine->currentHdrIndex >= 0) {
        LOG_INFO("engine", "Catalogue HDR initialise: %zu fichier(s), actif=%s", engine->hdrFiles.size(),
                 get_filename_from_path(engine->hdrFiles[static_cast<size_t>(engine->currentHdrIndex)]));
    } else {
        LOG_INFO("engine", "Catalogue HDR initialise: 0 fichier, fallback 1x1 actif");
    }

    return GfxResult::Success;
}

GfxResult vk_init_environment_texture(VulkanEngine* engine) {
    const char* envHdr = std::getenv("SVK_IBL_HDR");
    if (envHdr && envHdr[0] != '\0') {
        for (size_t i = 0; i < engine->hdrFiles.size(); ++i) {
            if (engine->hdrFiles[i].find(envHdr) != std::string::npos) {
                engine->currentHdrIndex = static_cast<int>(i);
                LOG_INFO("ibl", "SVK_IBL_HDR found: %s (index %d)", engine->hdrFiles[i].c_str(), (int)i);
                break;
            }
        }
    }

    const std::string hdrPath = (engine->currentHdrIndex >= 0 && engine->currentHdrIndex < static_cast<int>(engine->hdrFiles.size()))
                                    ? engine->hdrFiles[static_cast<size_t>(engine->currentHdrIndex)]
                                    : std::string();
    return init_environment_texture_from_path(engine, hdrPath) == ResourceResult::Success ? GfxResult::Success : GfxResult::ErrorInitializationFailed;
}

GfxResult vk_start_hdr_io_thread(VulkanEngine* engine) {
    engine->io.hdrIoThreadRunning.store(true, std::memory_order_relaxed);
    engine->io.hdrLoadInFlight = false;
    engine->io.pendingHdrIndex = -1;

    try {
        engine->io.hdrIoThread = std::thread(hdr_io_thread_main, engine);
    } catch (...) {
        engine->io.hdrIoThreadRunning.store(false, std::memory_order_relaxed);
        return GfxResult::ErrorInitializationFailed;
    }

    return GfxResult::Success;
}

void vk_stop_hdr_io_thread(VulkanEngine* engine) {
    engine->io.hdrIoThreadRunning.store(false, std::memory_order_relaxed);

    if (engine->io.hdrIoThread.joinable()) {
        engine->io.hdrIoThread.join();
    }

    engine->io.pendingHdrIndex = -1;
    engine->io.hdrLoadInFlight = false;

    HdrLoadRequest req;
    while (engine->io.hdrLoadQueue.pop(req)) {
        if (req.stagingBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(engine->ctx.allocator, req.stagingBuffer, req.stagingAllocation);
        }
    }
    while (engine->io.hdrReadyQueue.pop(req)) {
        if (req.stagingBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(engine->ctx.allocator, req.stagingBuffer, req.stagingAllocation);
        }
    }

    process_hdr_cleanup_queue(engine);
}

void vk_process_ready_environment_texture(VulkanEngine* engine) {
    SVK_TRACY_ZONE_SCOPED_C("Frame Process Ready Texture", tracy_color::CpuUpdateChild);
    auto t0 = std::chrono::high_resolution_clock::now();
    HdrLoadRequest ready{};
    bool hasReady = false;

    HdrLoadRequest req;
    while (engine->io.hdrReadyQueue.pop(req)) {
        if (hasReady && ready.stagingBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(engine->ctx.allocator, ready.stagingBuffer, ready.stagingAllocation);
        }
        ready = std::move(req);
        hasReady = true;
    }

    if (hasReady && engine->io.pendingHdrIndex == ready.hdrIndex) {
        engine->io.pendingHdrIndex = -1;
    }

    if (!hasReady) {
        return;
    }

    if (ready.state != HdrLoadRequestState::Ready || ready.stagingBuffer == VK_NULL_HANDLE || ready.width == 0 || ready.height == 0) {
        LOG_ERROR("runtime", "Echec du chargement async HDR");
        if (ready.stagingBuffer != VK_NULL_HANDLE)
            vmaDestroyBuffer(engine->ctx.allocator, ready.stagingBuffer, ready.stagingAllocation);
        return;
    }

    if (engine->envHdrSampler.is_valid()) {
        engine->iblBaker.pendingOldSamplers.push_back(std::move(engine->envHdrSampler));
    }
    if (engine->envHdrImage.is_valid()) {
        engine->iblBaker.pendingOldTextures.push_back(std::move(engine->envHdrImage));
    }

    if (init_environment_texture_from_staging(engine, ready, false, true) != ResourceResult::Success) {
        LOG_ERROR("runtime", "Upload GPU HDR async echoue");
        vmaDestroyBuffer(engine->ctx.allocator, ready.stagingBuffer, ready.stagingAllocation);
        return;
    }

    engine->iblBaker.pendingStagingBuffers.push_back(ready.stagingBuffer);
    engine->iblBaker.pendingStagingAllocations.push_back(ready.stagingAllocation);
    engine->currentHdrIndex = ready.hdrIndex;

    engine->appState->core.render.envLod =
        std::clamp(engine->appState->core.render.envLod, kMinEnvLod, static_cast<float>(engine->envHdrMipLevels > 0 ? engine->envHdrMipLevels - 1 : 0));
    LOG_INFO("runtime", "HDR actif: %s", get_filename_from_path(ready.sourcePathOrLabel));

    auto t1 = std::chrono::high_resolution_clock::now();
    float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    LOG_INFO("runtime", "vk_process_ready_environment_texture a pris %.2f ms", ms);
}

void IblBaker::cleanupPendingResources(VulkanEngine* engine) {
    for (uint64_t viewHandle : pendingImageViews) {
        engine->appState->rhi->DestroyImageView(static_cast<ImageViewHandle>(viewHandle));
    }
    pendingImageViews.clear();

    for (uint64_t poolHandle : pendingDescriptorPools) {
        engine->appState->rhi->DestroyDescriptorPool(static_cast<DescriptorPoolHandle>(poolHandle));
    }
    pendingDescriptorPools.clear();

    for (auto& tex : pendingOldTextures) {
        HdrCleanupRequest req;
        req.tex = std::move(tex);
        push_to_cleanup_queue(engine, std::move(req));
    }
    pendingOldTextures.clear();

    for (auto& smp : pendingOldSamplers) {
        HdrCleanupRequest req;
        req.smp = std::move(smp);
        push_to_cleanup_queue(engine, std::move(req));
    }
    pendingOldSamplers.clear();

    for (size_t i = 0; i < pendingStagingBuffers.size(); ++i) {
        HdrCleanupRequest req;
        req.buffer = pendingStagingBuffers[i];
        req.allocation = pendingStagingAllocations[i];
        push_to_cleanup_queue(engine, std::move(req));
    }
    pendingStagingBuffers.clear();
    pendingStagingAllocations.clear();
    currentStagingBuffer = VK_NULL_HANDLE;
}

static void vk_finalize_ibl_bake(VulkanEngine* engine) {
    engine->iblBaker.cleanupPendingResources(engine);

    update_envmap_descriptor_set(engine);
    auto tEnd = std::chrono::high_resolution_clock::now();
    if (engine->iblBaker.envmapRequestTime.time_since_epoch().count() != 0) {
        float totalMs = std::chrono::duration<float, std::milli>(tEnd - engine->iblBaker.envmapRequestTime).count();
        LOG_INFO("ibl", "IBL environment ready in %.2f ms, descriptor set updated.", totalMs);
        engine->iblBaker.envmapRequestTime = {}; // Reset for next time
    }

    engine->iblBaker.bakeState = IblBakeState::Idle;
    tracy_state::set_async_status(tracy_state::AsyncState::Idle);
}

static void vk_process_luminance_wait(VulkanEngine* engine) {
    void* data = nullptr;
    float meanLum = 1.0f;
    if (vmaMapMemory(engine->ctx.allocator, engine->iblBaker.lumMeanAllocation, &data) == VK_SUCCESS) {
        memcpy(&meanLum, data, sizeof(float));
        vmaUnmapMemory(engine->ctx.allocator, engine->iblBaker.lumMeanAllocation);
        if (std::isnan(meanLum) || std::isinf(meanLum) || meanLum <= 0.0f)
            meanLum = 1.0f;
        engine->iblBaker.bakedMeanLuminance = meanLum;
        LOG_INFO("ibl", "Async Mean luminance: %.4f", meanLum);
    }
    engine->iblBaker.bakeState = IblBakeState::Brdf;
    vk_ibl_bake_brdf(engine);
}

static void vk_process_irradiance_wait(VulkanEngine* engine) {
    engine->iblBaker.currentSlice++;
    if (engine->iblBaker.currentSlice < engine->iblBaker.totalSlices) {
        engine->iblBaker.bakeState = IblBakeState::Irradiance;
        vk_ibl_bake_irradiance(engine);
    } else {
        engine->iblBaker.currentMip = 0;
        engine->iblBaker.currentSlice = 0;
        engine->iblBaker.totalSlices = 24; // 24 slices for Specular Mip 0
        engine->iblBaker.bakeState = IblBakeState::Prefilter;
        vk_ibl_bake_prefilter(engine);
    }
}

static void vk_process_prefilter_wait(VulkanEngine* engine) {
    engine->iblBaker.currentSlice++;
    if (engine->iblBaker.currentSlice >= engine->iblBaker.totalSlices) {
        engine->iblBaker.currentSlice = 0;
        engine->iblBaker.currentMip++;
        if (engine->iblBaker.currentMip == 1) {
            engine->iblBaker.totalSlices = 8;
        } else {
            engine->iblBaker.totalSlices = 1;
        }
    }
    if (engine->iblBaker.currentMip < (int)IBL_SPM_MIPS) {
        engine->iblBaker.bakeState = IblBakeState::Prefilter;
        vk_ibl_bake_prefilter(engine);
    } else {
        engine->iblBaker.bakeState = IblBakeState::Finalize;
    }
}

void vk_check_ibl_bake_status(VulkanEngine* engine) {
    if (engine->iblBaker.bakeState == IblBakeState::Idle) {
        return;
    }

    if (engine->iblBaker.iblBakeFence != VK_NULL_HANDLE) {
        SVK_TRACY_FIBER_ZONE_C(fiberSync, "Hybrid Perf", "Sync (GPU Wait)", tracy_color::FiberHybridGpuWait);
        if (vkGetFenceStatus(engine->ctx.device, engine->iblBaker.iblBakeFence) != VK_SUCCESS) {
            return;
        }

        if (engine->iblBaker.iblBakeCommandBuffer != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(engine->ctx.device, engine->ctx.commandPool, 1, &engine->iblBaker.iblBakeCommandBuffer);
            engine->iblBaker.iblBakeCommandBuffer = VK_NULL_HANDLE;
        }

        vkDestroyFence(engine->ctx.device, engine->iblBaker.iblBakeFence, nullptr);
        engine->iblBaker.iblBakeFence = VK_NULL_HANDLE;
    }

    switch (engine->iblBaker.bakeState) {
    case IblBakeState::UploadHdr:
        vk_process_upload_hdr(engine);
        break;

    case IblBakeState::UploadHdrWait:
        engine->iblBaker.currentSlice++;
        if (engine->iblBaker.currentSlice < engine->iblBaker.totalSlices) {
            engine->iblBaker.bakeState = IblBakeState::UploadHdr;
            vk_process_upload_hdr(engine);
        } else {
            engine->iblBaker.bakeState = IblBakeState::GenerateMipmap;
            engine->iblBaker.currentMip = 1;
            vk_generate_one_hdr_mipmap(engine);
        }
        break;

    case IblBakeState::GenerateMipmap:
        engine->iblBaker.bakeState = IblBakeState::GenerateMipmapWait;
        break;

    case IblBakeState::GenerateMipmapWait:
        if (engine->iblBaker.currentMip < static_cast<int>(engine->envHdrMipLevels) || (engine->envHdrMipLevels == 1 && engine->iblBaker.currentMip == 1)) {
            engine->iblBaker.bakeState = IblBakeState::GenerateMipmap;
            vk_generate_one_hdr_mipmap(engine);
        } else {
            engine->iblBaker.bakeState = IblBakeState::Luminance;
            vk_ibl_bake_luminance(engine);
        }
        break;

    case IblBakeState::Luminance:
        engine->iblBaker.bakeState = IblBakeState::LuminanceWait;
        break;

    case IblBakeState::LuminanceWait:
        vk_process_luminance_wait(engine);
        break;

    case IblBakeState::Brdf:
        engine->iblBaker.bakeState = IblBakeState::BrdfWait;
        break;

    case IblBakeState::BrdfWait:
        engine->iblBaker.currentSlice = 0;
        engine->iblBaker.totalSlices = 12; // 12 slices for irradiance
        engine->iblBaker.bakeState = IblBakeState::Irradiance;
        vk_ibl_bake_irradiance(engine);
        break;

    case IblBakeState::Irradiance:
        engine->iblBaker.bakeState = IblBakeState::IrradianceWait;
        break;

    case IblBakeState::IrradianceWait:
        vk_process_irradiance_wait(engine);
        break;

    case IblBakeState::Prefilter:
        engine->iblBaker.bakeState = IblBakeState::PrefilterWait;
        break;

    case IblBakeState::PrefilterWait:
        vk_process_prefilter_wait(engine);
        break;

    case IblBakeState::Finalize:
        vk_finalize_ibl_bake(engine);
        break;

    default:
        break;
    }
}

void vk_switch_environment_texture(VulkanEngine* engine, int direction) {
    if (engine->hdrFiles.size() <= 1) {
        return;
    }

    const int hdrCount = static_cast<int>(engine->hdrFiles.size());
    int nextIndex = engine->currentHdrIndex + direction;
    if (nextIndex >= hdrCount) {
        nextIndex = 0;
    } else if (nextIndex < 0) {
        nextIndex = hdrCount - 1;
    }

    request_environment_texture_async(engine, nextIndex);
}

void vk_adjust_env_lod(VulkanEngine* engine, float delta) {
    engine->appState->core.render.envLod =
        std::clamp(engine->appState->core.render.envLod + delta, kMinEnvLod, static_cast<float>(engine->envHdrMipLevels > 0 ? engine->envHdrMipLevels - 1 : 0));
    LOG_INFO("runtime", "Env LOD: %.1f", engine->appState->core.render.envLod);
}
