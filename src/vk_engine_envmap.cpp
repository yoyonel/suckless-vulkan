#include "vk_engine_envmap.h"
#include "asset_ktx.h"
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
    irrInfo.texture = engine->ibl.irradianceMap.is_valid() ? engine->ibl.irradianceMap : engine->envHdrImage;
    irrInfo.imageView = INVALID_HANDLE;
    irrInfo.sampler = engine->ibl.irradianceSampler.is_valid() ? engine->ibl.irradianceSampler : engine->envHdrSampler;

    DescriptorImageInfo prefInfo{};
    prefInfo.imageLayout = TextureLayout::ShaderReadOnlyOptimal;
    prefInfo.texture = engine->ibl.prefilteredMap.is_valid() ? engine->ibl.prefilteredMap : engine->envHdrImage;
    prefInfo.imageView = INVALID_HANDLE;
    prefInfo.sampler = engine->ibl.prefilteredSampler.is_valid() ? engine->ibl.prefilteredSampler : engine->envHdrSampler;

    DescriptorImageInfo lutInfo{};
    lutInfo.imageLayout = TextureLayout::ShaderReadOnlyOptimal;
    lutInfo.texture = engine->ibl.brdfLut.is_valid() ? engine->ibl.brdfLut : engine->envHdrImage;
    lutInfo.imageView = INVALID_HANDLE;
    lutInfo.sampler = engine->ibl.brdfLutSampler.is_valid() ? engine->ibl.brdfLutSampler : engine->envHdrSampler;

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

std::string get_filename_from_path(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

VkCommandBuffer begin_one_time_commands(VulkanEngine* engine) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = engine->commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(engine->device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    vk_set_object_name(engine->device, (uint64_t)commandBuffer, VK_OBJECT_TYPE_COMMAND_BUFFER, "EnvHDR_Transfer_CommandBuffer");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &commandBuffer);
        return VK_NULL_HANDLE;
    }

    return commandBuffer;
}

ResourceResult end_one_time_commands(VulkanEngine* engine, VkCommandBuffer commandBuffer) {
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &commandBuffer);
        return ResourceResult::ErrorParseFailed;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(engine->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS || vkQueueWaitIdle(engine->graphicsQueue) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &commandBuffer);
        return ResourceResult::ErrorParseFailed;
    }

    vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &commandBuffer);
    return ResourceResult::Success;
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

void generate_hdr_mipmaps(VulkanEngine* engine, VkCommandBuffer commandBuffer, int32_t width, int32_t height) {
    int32_t mipWidth = width;
    int32_t mipHeight = height;
    for (uint32_t i = 1; i < engine->envHdrMipLevels; ++i) {
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

        mipWidth = std::max(1, mipWidth / 2);
        mipHeight = std::max(1, mipHeight / 2);
    }

    (void)transition_hdr_image_layout(engine, commandBuffer, engine->envHdrMipLevels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

} // namespace

ResourceResult init_environment_texture_from_staging(VulkanEngine* engine, VkBuffer stagingBuffer, int width, int height, const std::string& sourceLabel,
                                                     bool isFallback) {
    SVK_TRACY_ZONE_SCOPED("init_environment_texture_from_staging");
    if (stagingBuffer == VK_NULL_HANDLE || width <= 0 || height <= 0) {
        return ResourceResult::ErrorParseFailed;
    }

    engine->envHdrMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    VkFormatProperties hdrFormatProps{};
    vkGetPhysicalDeviceFormatProperties(engine->physicalDevice, VK_FORMAT_R32G32B32A32_SFLOAT, &hdrFormatProps);
    const bool canLinearBlit = (hdrFormatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
    if (!canLinearBlit) {
        engine->envHdrMipLevels = 1;
    }

    vk_set_object_name(engine->device, (uint64_t)stagingBuffer, VK_OBJECT_TYPE_BUFFER, "EnvHDR_Staging_Buffer");

    engine->envHdrWidth = static_cast<uint32_t>(width);
    engine->envHdrHeight = static_cast<uint32_t>(height);

    engine->envHdrImage.Reset(engine->appState->rhi,
                              engine->appState->rhi->CreateTexture(engine->envHdrWidth, engine->envHdrHeight, TextureFormat::RGBA32_SFLOAT,
                                                                   TextureUsage::Sampled, engine->envHdrMipLevels, "EnvHDR_Image"));
    if (!engine->envHdrImage.is_valid()) {
        return ResourceResult::ErrorParseFailed;
    }

    VkImage vkEnvHdrImage = ((VulkanRHI*)engine->appState->rhi)->GetVkImage(engine->envHdrImage);

    VkCommandBuffer commandBuffer = begin_one_time_commands(engine);
    if (commandBuffer == VK_NULL_HANDLE) {
        return ResourceResult::ErrorParseFailed;
    }

    vk_begin_label(engine->device, commandBuffer, "Upload_EnvHDR_Texture", 0.0f, 0.8f, 1.0f);

    (void)transition_hdr_image_layout(engine, commandBuffer, 0, engine->envHdrMipLevels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    vk_begin_label(engine->device, commandBuffer, "Copy_EnvHDR_Staging_To_Image", 0.0f, 0.6f, 1.0f);
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    ((VulkanRHI*)engine->appState->rhi)->CmdCopyBufferToImage(commandBuffer, stagingBuffer, vkEnvHdrImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    vk_end_label(engine->device, commandBuffer);

    vk_begin_label(engine->device, commandBuffer, "Generate_EnvHDR_Mipmaps", 0.0f, 0.4f, 0.8f);
    generate_hdr_mipmaps(engine, commandBuffer, width, height);
    vk_end_label(engine->device, commandBuffer);
    vk_end_label(engine->device, commandBuffer);

    if (end_one_time_commands(engine, commandBuffer) != ResourceResult::Success) {
        return ResourceResult::ErrorParseFailed;
    }

    engine->envHdrSampler.Reset(engine->appState->rhi, engine->appState->rhi->CreateSampler(engine->envHdrMipLevels, "EnvHDR_Sampler"));
    if (!engine->envHdrSampler.is_valid()) {
        return ResourceResult::ErrorParseFailed;
    }

    if (isFallback) {
        LOG_INFO("engine", "HDR map fallback 1x1 initialisee (%dx%d, mips=%u)", width, height, engine->envHdrMipLevels);
    } else {
        LOG_INFO("engine", "HDR map chargee: %s (%dx%d, mips=%u)", sourceLabel.c_str(), width, height, engine->envHdrMipLevels);
    }

    // Phase IBL-0: Synchronous Bake
    // The envHdrImageView is now valid, we can bake IBL maps
    vk_ibl_bake(engine);

    // Update global descriptor set with the new IBL maps
    update_envmap_descriptor_set(engine);

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
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        if (vmaCreateBuffer(engine->allocator, &bufferInfo, &stagingAllocInfo, &request->stagingBuffer, &request->stagingAllocation, nullptr) != VK_SUCCESS) {
            return nullptr;
        }

        void* mapped = nullptr;
        if (vmaMapMemory(engine->allocator, request->stagingAllocation, &mapped) != VK_SUCCESS) {
            vmaDestroyBuffer(engine->allocator, request->stagingBuffer, request->stagingAllocation);
            request->stagingBuffer = VK_NULL_HANDLE;
            return nullptr;
        }
        return mapped;
    };

    if (ktxValid) {
        int width;
        int height;
        if (ktx2_load_from_file(ktxPath, &width, &height, allocator_func) == KtxResult::Success) {
            LOG_INFO("engine", "[KTX Cache] Load fast-path: %s", ktxPath.c_str());
            vmaUnmapMemory(engine->allocator, request->stagingAllocation);
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
            vmaUnmapMemory(engine->allocator, request->stagingAllocation);

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
    HdrLoadRequest request{};

    if (hdrPath.empty()) {
        LOG_INFO("engine", "Aucun fichier HDR trouve dans assets/textures/hdr, utilisation d'une texture fallback 1x1");
        float fallbackPixel[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        // Quick setup for fallback staging
        VkBufferCreateInfo bufferInfo{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, 16, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        vmaCreateBuffer(engine->allocator, &bufferInfo, &stagingAllocInfo, &request.stagingBuffer, &request.stagingAllocation, nullptr);
        void* mapped;
        vmaMapMemory(engine->allocator, request.stagingAllocation, &mapped);
        memcpy(mapped, fallbackPixel, 16);
        vmaUnmapMemory(engine->allocator, request.stagingAllocation);
        ResourceResult res = init_environment_texture_from_staging(engine, request.stagingBuffer, 1, 1, "fallback-1x1", true);
        vmaDestroyBuffer(engine->allocator, request.stagingBuffer, request.stagingAllocation);
        return res;
    }

    if (load_hdr_with_ktx2_cache(engine, hdrPath, &request) != ResourceResult::Success || request.width == 0) {
        LOG_WARNING("engine", "Echec du chargement HDR: %s, utilisation d'une texture fallback 1x1", hdrPath.c_str());
        float fallbackPixel[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        VkBufferCreateInfo bufferInfo{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, 16, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        vmaCreateBuffer(engine->allocator, &bufferInfo, &stagingAllocInfo, &request.stagingBuffer, &request.stagingAllocation, nullptr);
        void* mapped;
        vmaMapMemory(engine->allocator, request.stagingAllocation, &mapped);
        memcpy(mapped, fallbackPixel, 16);
        vmaUnmapMemory(engine->allocator, request.stagingAllocation);
        ResourceResult res = init_environment_texture_from_staging(engine, request.stagingBuffer, 1, 1, "fallback-1x1", true);
        vmaDestroyBuffer(engine->allocator, request.stagingBuffer, request.stagingAllocation);
        return res;
    }

    ResourceResult res =
        init_environment_texture_from_staging(engine, request.stagingBuffer, static_cast<int>(request.width), static_cast<int>(request.height), hdrPath, false);
    vmaDestroyBuffer(engine->allocator, request.stagingBuffer, request.stagingAllocation);
    return res;
}

void request_environment_texture_async(VulkanEngine* engine, int newHdrIndex) {
    SVK_TRACY_ZONE_SCOPED("request_environment_texture_async");
    if (newHdrIndex < 0 || newHdrIndex >= static_cast<int>(engine->hdrFiles.size()) || newHdrIndex == engine->currentHdrIndex) {
        return;
    }

    std::lock_guard<std::mutex> lock(engine->hdrLoadMutex);
    if (engine->pendingHdrIndex == newHdrIndex) {
        return;
    }

    while (!engine->hdrLoadQueue.empty()) {
        engine->hdrLoadQueue.pop();
    }

    HdrLoadRequest request{};
    request.hdrIndex = newHdrIndex;
    request.state = HdrLoadRequestState::Pending;
    request.width = 0;
    request.height = 0;
    request.channels = 0;
    engine->hdrLoadQueue.push(std::move(request));
    engine->pendingHdrIndex = newHdrIndex;
    engine->hdrLoadCV.notify_one();

    LOG_INFO("runtime", "Chargement HDR async demande: %s", get_filename_from_path(engine->hdrFiles[static_cast<size_t>(newHdrIndex)]).c_str());
}

void hdr_io_thread_main(VulkanEngine* engine) {
#ifdef TRACY_ENABLE
    TracyCSetThreadName("HDR I/O Thread");
#endif

    for (;;) {
        SVK_TRACY_ZONE_SCOPED("hdr_io_thread_iteration");
        HdrLoadRequest request{};

        {
            std::unique_lock<std::mutex> lock(engine->hdrLoadMutex);
            engine->hdrLoadCV.wait(lock, [engine] { return !engine->hdrIoThreadRunning || !engine->hdrLoadQueue.empty(); });
            if (!engine->hdrIoThreadRunning && engine->hdrLoadQueue.empty()) {
                return;
            }

            request = std::move(engine->hdrLoadQueue.front());
            engine->hdrLoadQueue.pop();
            request.state = HdrLoadRequestState::Loading;
            engine->hdrLoadInFlight = true;
        }

        if (request.hdrIndex >= 0 && request.hdrIndex < static_cast<int>(engine->hdrFiles.size())) {
            SVK_TRACY_ZONE_SCOPED("hdr_io_thread_decode");
            const std::string hdrPath = engine->hdrFiles[static_cast<size_t>(request.hdrIndex)];

            if (load_hdr_with_ktx2_cache(engine, hdrPath, &request) == ResourceResult::Success && request.width > 0 && request.height > 0) {
                request.channels = 4;
                request.sourcePathOrLabel = hdrPath;
                request.state = HdrLoadRequestState::Ready;
            } else {
                request.state = HdrLoadRequestState::Failed;
            }
        } else {
            request.state = HdrLoadRequestState::Failed;
        }

        {
            std::lock_guard<std::mutex> lock(engine->hdrLoadMutex);
            engine->hdrReadyQueue.push(std::move(request));
            engine->hdrLoadInFlight = false;
        }
    }
}

} // namespace

void vk_cleanup_environment_resources(VulkanEngine* engine) {
    if (engine->device == VK_NULL_HANDLE) {
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
                 get_filename_from_path(engine->hdrFiles[static_cast<size_t>(engine->currentHdrIndex)]).c_str());
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
    {
        std::lock_guard<std::mutex> lock(engine->hdrLoadMutex);
        engine->hdrIoThreadRunning = true;
        engine->hdrLoadInFlight = false;
        engine->pendingHdrIndex = -1;
    }

    try {
        engine->hdrIoThread = std::thread(hdr_io_thread_main, engine);
    } catch (...) {
        std::lock_guard<std::mutex> lock(engine->hdrLoadMutex);
        engine->hdrIoThreadRunning = false;
        return GfxResult::ErrorInitializationFailed;
    }

    return GfxResult::Success;
}

void vk_stop_hdr_io_thread(VulkanEngine* engine) {
    {
        std::lock_guard<std::mutex> lock(engine->hdrLoadMutex);
        engine->hdrIoThreadRunning = false;
        engine->pendingHdrIndex = -1;
    }
    engine->hdrLoadCV.notify_all();

    if (engine->hdrIoThread.joinable()) {
        engine->hdrIoThread.join();
    }

    std::lock_guard<std::mutex> lock(engine->hdrLoadMutex);
    engine->hdrLoadInFlight = false;
    while (!engine->hdrLoadQueue.empty()) {
        if (engine->hdrLoadQueue.front().stagingBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(engine->allocator, engine->hdrLoadQueue.front().stagingBuffer, engine->hdrLoadQueue.front().stagingAllocation);
        }
        engine->hdrLoadQueue.pop();
    }
    while (!engine->hdrReadyQueue.empty()) {
        if (engine->hdrReadyQueue.front().stagingBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(engine->allocator, engine->hdrReadyQueue.front().stagingBuffer, engine->hdrReadyQueue.front().stagingAllocation);
        }
        engine->hdrReadyQueue.pop();
    }
}

void vk_process_ready_environment_texture(VulkanEngine* engine) {
    SVK_TRACY_ZONE_SCOPED("vk_process_ready_environment_texture");
    HdrLoadRequest ready{};
    bool hasReady = false;

    {
        std::lock_guard<std::mutex> lock(engine->hdrLoadMutex);
        while (!engine->hdrReadyQueue.empty()) {
            ready = std::move(engine->hdrReadyQueue.front());
            engine->hdrReadyQueue.pop();
            hasReady = true;
        }
        if (hasReady && engine->pendingHdrIndex == ready.hdrIndex) {
            engine->pendingHdrIndex = -1;
        }
    }

    if (!hasReady) {
        return;
    }

    if (ready.state != HdrLoadRequestState::Ready || ready.stagingBuffer == VK_NULL_HANDLE || ready.width == 0 || ready.height == 0) {
        LOG_ERROR("runtime", "Echec du chargement async HDR");
        if (ready.stagingBuffer != VK_NULL_HANDLE)
            vmaDestroyBuffer(engine->allocator, ready.stagingBuffer, ready.stagingAllocation);
        return;
    }

    if (vkDeviceWaitIdle(engine->device) != VK_SUCCESS) {
        LOG_ERROR("runtime", "Impossible de synchroniser le device avant upload HDR async");
        vmaDestroyBuffer(engine->allocator, ready.stagingBuffer, ready.stagingAllocation);
        return;
    }

    vk_cleanup_environment_resources(engine);
    if (init_environment_texture_from_staging(engine, ready.stagingBuffer, static_cast<int>(ready.width), static_cast<int>(ready.height),
                                              ready.sourcePathOrLabel, false) != ResourceResult::Success) {
        LOG_ERROR("runtime", "Upload GPU HDR async echoue");
        vmaDestroyBuffer(engine->allocator, ready.stagingBuffer, ready.stagingAllocation);
        return;
    }
    vmaDestroyBuffer(engine->allocator, ready.stagingBuffer, ready.stagingAllocation);

    engine->currentHdrIndex = ready.hdrIndex;

    // Phase IBL-0: Synchronous Bake
    // The envHdrImageView is now valid, we can bake IBL maps
    vk_ibl_bake(engine);

    // Update descriptors including IBL maps
    update_envmap_descriptor_set(engine);

    engine->appState->core.render.envLod =
        std::clamp(engine->appState->core.render.envLod, kMinEnvLod, static_cast<float>(engine->envHdrMipLevels > 0 ? engine->envHdrMipLevels - 1 : 0));
    LOG_INFO("runtime", "HDR actif: %s", get_filename_from_path(ready.sourcePathOrLabel).c_str());
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
