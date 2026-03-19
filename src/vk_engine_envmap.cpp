#include "vk_engine_envmap.h"

#include "app_log.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
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

bool end_one_time_commands(VulkanEngine* engine, VkCommandBuffer commandBuffer) {
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &commandBuffer);
        return false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(engine->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS || vkQueueWaitIdle(engine->graphicsQueue) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &commandBuffer);
        return false;
    }

    vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &commandBuffer);
    return true;
}

bool transition_hdr_image_layout(VulkanEngine* engine, VkCommandBuffer commandBuffer, uint32_t baseMipLevel, uint32_t levelCount, VkImageLayout oldLayout,
                                 VkImageLayout newLayout, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkPipelineStageFlags srcStage,
                                 VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = engine->envHdrImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;

    vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    return true;
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

bool create_hdr_staging_buffer(VulkanEngine* engine, const float* pixels, int width, int height, VkBuffer& stagingBuffer, VmaAllocation& stagingAllocation) {
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4 * sizeof(float);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    if (vmaCreateBuffer(engine->allocator, &bufferInfo, &stagingAllocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    void* mapped = nullptr;
    if (vmaMapMemory(engine->allocator, stagingAllocation, &mapped) != VK_SUCCESS) {
        vmaDestroyBuffer(engine->allocator, stagingBuffer, stagingAllocation);
        return false;
    }
    memcpy(mapped, pixels, static_cast<size_t>(imageSize));
    vmaUnmapMemory(engine->allocator, stagingAllocation);
    return true;
}

void generate_hdr_mipmaps(VulkanEngine* engine, VkCommandBuffer commandBuffer, int32_t width, int32_t height) {
    int32_t mipWidth = width;
    int32_t mipHeight = height;
    for (uint32_t i = 1; i < engine->envHdrMipLevels; ++i) {
        transition_hdr_image_layout(engine, commandBuffer, i - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

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

        vkCmdBlitImage(commandBuffer, engine->envHdrImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, engine->envHdrImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                       &blit, VK_FILTER_LINEAR);

        transition_hdr_image_layout(engine, commandBuffer, i - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        mipWidth = std::max(1, mipWidth / 2);
        mipHeight = std::max(1, mipHeight / 2);
    }

    transition_hdr_image_layout(engine, commandBuffer, engine->envHdrMipLevels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

} // namespace

bool init_environment_texture_from_pixels(VulkanEngine* engine, const float* pixels, int width, int height, const std::string& sourceLabel, bool isFallback) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    engine->envHdrMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    VkFormatProperties hdrFormatProps{};
    vkGetPhysicalDeviceFormatProperties(engine->physicalDevice, VK_FORMAT_R32G32B32A32_SFLOAT, &hdrFormatProps);
    const bool canLinearBlit = (hdrFormatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
    if (!canLinearBlit) {
        engine->envHdrMipLevels = 1;
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;

    if (!create_hdr_staging_buffer(engine, pixels, width, height, stagingBuffer, stagingAllocation)) {
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)stagingBuffer, VK_OBJECT_TYPE_BUFFER, "EnvHDR_Staging_Buffer");

    engine->envHdrWidth = static_cast<uint32_t>(width);
    engine->envHdrHeight = static_cast<uint32_t>(height);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = engine->envHdrWidth;
    imageInfo.extent.height = engine->envHdrHeight;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = engine->envHdrMipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo imageAllocInfo{};
    imageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateImage(engine->allocator, &imageInfo, &imageAllocInfo, &engine->envHdrImage, &engine->envHdrImageAllocation, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(engine->allocator, stagingBuffer, stagingAllocation);
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->envHdrImage, VK_OBJECT_TYPE_IMAGE, "EnvHDR_Image");

    VkCommandBuffer commandBuffer = begin_one_time_commands(engine);
    if (commandBuffer == VK_NULL_HANDLE) {
        vmaDestroyBuffer(engine->allocator, stagingBuffer, stagingAllocation);
        return false;
    }

    vk_begin_label(engine->device, commandBuffer, "Upload_EnvHDR_Texture", 0.0f, 0.8f, 1.0f);

    transition_hdr_image_layout(engine, commandBuffer, 0, engine->envHdrMipLevels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                                VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    vk_begin_label(engine->device, commandBuffer, "Copy_EnvHDR_Staging_To_Image", 0.0f, 0.6f, 1.0f);
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, engine->envHdrImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    vk_end_label(engine->device, commandBuffer);

    vk_begin_label(engine->device, commandBuffer, "Generate_EnvHDR_Mipmaps", 0.0f, 0.4f, 0.8f);
    generate_hdr_mipmaps(engine, commandBuffer, width, height);
    vk_end_label(engine->device, commandBuffer);
    vk_end_label(engine->device, commandBuffer);

    if (!end_one_time_commands(engine, commandBuffer)) {
        vmaDestroyBuffer(engine->allocator, stagingBuffer, stagingAllocation);
        return false;
    }

    vmaDestroyBuffer(engine->allocator, stagingBuffer, stagingAllocation);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = engine->envHdrImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = engine->envHdrMipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(engine->device, &viewInfo, nullptr, &engine->envHdrImageView) != VK_SUCCESS) {
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->envHdrImageView, VK_OBJECT_TYPE_IMAGE_VIEW, "EnvHDR_ImageView");

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = static_cast<float>(engine->envHdrMipLevels - 1);
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxAnisotropy = 1.0f;
    if (vkCreateSampler(engine->device, &samplerInfo, nullptr, &engine->envHdrSampler) != VK_SUCCESS) {
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->envHdrSampler, VK_OBJECT_TYPE_SAMPLER, "EnvHDR_Sampler");

    if (isFallback) {
        LOG_INFO("engine", "HDR map fallback 1x1 initialisee (%dx%d, mips=%u)", width, height, engine->envHdrMipLevels);
    } else {
        LOG_INFO("engine", "HDR map chargee: %s (%dx%d, mips=%u)", sourceLabel.c_str(), width, height, engine->envHdrMipLevels);
    }

    // Phase IBL-0: Synchronous Bake
    // The envHdrImageView is now valid, we can bake IBL maps
    vk_ibl_bake(engine);

    // Update global descriptor set with the new IBL maps
    if (engine->descriptorSet != VK_NULL_HANDLE) {
        VkDescriptorImageInfo envInfo{};
        envInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        envInfo.imageView = engine->envHdrImageView;
        envInfo.sampler = engine->envHdrSampler;

        VkDescriptorImageInfo irrInfo{};
        irrInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        irrInfo.imageView = engine->ibl.irradianceMapView ? engine->ibl.irradianceMapView : engine->envHdrImageView;
        irrInfo.sampler = engine->ibl.irradianceSampler ? engine->ibl.irradianceSampler : engine->envHdrSampler;

        VkDescriptorImageInfo prefInfo{};
        prefInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        prefInfo.imageView = engine->ibl.prefilteredMapView ? engine->ibl.prefilteredMapView : engine->envHdrImageView;
        prefInfo.sampler = engine->ibl.prefilteredSampler ? engine->ibl.prefilteredSampler : engine->envHdrSampler;

        VkDescriptorImageInfo lutInfo{};
        lutInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        lutInfo.imageView = engine->ibl.brdfLutView ? engine->ibl.brdfLutView : engine->envHdrImageView;
        lutInfo.sampler = engine->ibl.brdfLutSampler ? engine->ibl.brdfLutSampler : engine->envHdrSampler;

        VkWriteDescriptorSet writes[4] = {};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = engine->descriptorSet;
        writes[0].dstBinding = 1;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &envInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = engine->descriptorSet;
        writes[1].dstBinding = 2;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &irrInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = engine->descriptorSet;
        writes[2].dstBinding = 3;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].pImageInfo = &prefInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = engine->descriptorSet;
        writes[3].dstBinding = 4;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].pImageInfo = &lutInfo;

        vkUpdateDescriptorSets(engine->device, 4, writes, 0, nullptr);
    }

    return true;
}

bool init_environment_texture_from_path(VulkanEngine* engine, const std::string& hdrPath) {
    int width = 1;
    int height = 1;
    int channels = 0;
    float* loadedPixels = nullptr;
    std::vector<float> fallbackPixels;

    if (hdrPath.empty()) {
        LOG_INFO("engine", "Aucun fichier HDR trouve dans assets/textures/hdr, utilisation d'une texture fallback 1x1");
        fallbackPixels = {0.0f, 0.0f, 0.0f, 1.0f};
        return init_environment_texture_from_pixels(engine, fallbackPixels.data(), width, height, "fallback-1x1", true);
    }

    loadedPixels = stbi_loadf(hdrPath.c_str(), &width, &height, &channels, 4);
    if (loadedPixels == nullptr || width <= 0 || height <= 0) {
        LOG_WARNING("engine", "Echec du chargement HDR: %s, utilisation d'une texture fallback 1x1", hdrPath.c_str());
        fallbackPixels = {0.0f, 0.0f, 0.0f, 1.0f};
        return init_environment_texture_from_pixels(engine, fallbackPixels.data(), 1, 1, "fallback-1x1", true);
    }

    const bool ok = init_environment_texture_from_pixels(engine, loadedPixels, width, height, hdrPath, false);
    stbi_image_free(loadedPixels);
    return ok;
}

void request_environment_texture_async(VulkanEngine* engine, int newHdrIndex) {
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
    for (;;) {
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
            const std::string hdrPath = engine->hdrFiles[static_cast<size_t>(request.hdrIndex)];
            int width = 0;
            int height = 0;
            int channels = 0;
            float* pixels = stbi_loadf(hdrPath.c_str(), &width, &height, &channels, 4);
            if (pixels != nullptr && width > 0 && height > 0) {
                request.width = static_cast<uint32_t>(width);
                request.height = static_cast<uint32_t>(height);
                request.channels = 4;
                request.sourcePathOrLabel = hdrPath;
                request.pixelData.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4U);
                memcpy(request.pixelData.data(), pixels, request.pixelData.size() * sizeof(float));
                request.state = HdrLoadRequestState::Ready;
            } else {
                request.state = HdrLoadRequestState::Failed;
            }
            if (pixels != nullptr) {
                stbi_image_free(pixels);
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

    if (engine->envHdrSampler != VK_NULL_HANDLE) {
        vkDestroySampler(engine->device, engine->envHdrSampler, nullptr);
        engine->envHdrSampler = VK_NULL_HANDLE;
    }
    if (engine->envHdrImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(engine->device, engine->envHdrImageView, nullptr);
        engine->envHdrImageView = VK_NULL_HANDLE;
    }
    if (engine->allocator != VK_NULL_HANDLE && engine->envHdrImage != VK_NULL_HANDLE && engine->envHdrImageAllocation != VK_NULL_HANDLE) {
        vmaDestroyImage(engine->allocator, engine->envHdrImage, engine->envHdrImageAllocation);
        engine->envHdrImage = VK_NULL_HANDLE;
        engine->envHdrImageAllocation = VK_NULL_HANDLE;
    }

    engine->envHdrMipLevels = 0;
}

bool vk_init_environment_catalog(VulkanEngine* engine) {
    engine->hdrFiles = find_hdr_paths();
    engine->currentHdrIndex = find_default_hdr_index(engine->hdrFiles);

    if (engine->currentHdrIndex >= 0) {
        LOG_INFO("engine", "Catalogue HDR initialise: %zu fichier(s), actif=%s", engine->hdrFiles.size(),
                 get_filename_from_path(engine->hdrFiles[static_cast<size_t>(engine->currentHdrIndex)]).c_str());
    } else {
        LOG_INFO("engine", "Catalogue HDR initialise: 0 fichier, fallback 1x1 actif");
    }

    return true;
}

bool vk_init_environment_texture(VulkanEngine* engine) {
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
    return init_environment_texture_from_path(engine, hdrPath);
}

bool vk_start_hdr_io_thread(VulkanEngine* engine) {
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
        return false;
    }

    return true;
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
        engine->hdrLoadQueue.pop();
    }
    while (!engine->hdrReadyQueue.empty()) {
        engine->hdrReadyQueue.pop();
    }
}

void vk_process_ready_environment_texture(VulkanEngine* engine) {
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

    if (ready.state != HdrLoadRequestState::Ready || ready.pixelData.empty() || ready.width == 0 || ready.height == 0) {
        LOG_ERROR("runtime", "Echec du chargement async HDR");
        return;
    }

    if (vkDeviceWaitIdle(engine->device) != VK_SUCCESS) {
        LOG_ERROR("runtime", "Impossible de synchroniser le device avant upload HDR async");
        return;
    }

    vk_cleanup_environment_resources(engine);
    if (!init_environment_texture_from_pixels(engine, ready.pixelData.data(), static_cast<int>(ready.width), static_cast<int>(ready.height),
                                              ready.sourcePathOrLabel, false)) {
        LOG_ERROR("runtime", "Upload GPU HDR async echoue");
        return;
    }

    engine->currentHdrIndex = ready.hdrIndex;

    // Phase IBL-0: Synchronous Bake
    // The envHdrImageView is now valid, we can bake IBL maps
    vk_ibl_bake(engine);

    // Update descriptors including IBL maps
    if (engine->descriptorSet != VK_NULL_HANDLE) {
        VkDescriptorImageInfo envInfo{};
        envInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        envInfo.imageView = engine->envHdrImageView;
        envInfo.sampler = engine->envHdrSampler;

        VkDescriptorImageInfo irrInfo{};
        irrInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        irrInfo.imageView = engine->ibl.irradianceMapView ? engine->ibl.irradianceMapView : engine->envHdrImageView;
        irrInfo.sampler = engine->ibl.irradianceSampler ? engine->ibl.irradianceSampler : engine->envHdrSampler;

        VkDescriptorImageInfo prefInfo{};
        prefInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        prefInfo.imageView = engine->ibl.prefilteredMapView ? engine->ibl.prefilteredMapView : engine->envHdrImageView;
        prefInfo.sampler = engine->ibl.prefilteredSampler ? engine->ibl.prefilteredSampler : engine->envHdrSampler;

        VkDescriptorImageInfo lutInfo{};
        lutInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        lutInfo.imageView = engine->ibl.brdfLutView ? engine->ibl.brdfLutView : engine->envHdrImageView;
        lutInfo.sampler = engine->ibl.brdfLutSampler ? engine->ibl.brdfLutSampler : engine->envHdrSampler;

        VkWriteDescriptorSet writes[4] = {};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = engine->descriptorSet;
        writes[0].dstBinding = 1;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &envInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = engine->descriptorSet;
        writes[1].dstBinding = 2;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &irrInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = engine->descriptorSet;
        writes[2].dstBinding = 3;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].pImageInfo = &prefInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = engine->descriptorSet;
        writes[3].dstBinding = 4;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].pImageInfo = &lutInfo;

        vkUpdateDescriptorSets(engine->device, 4, writes, 0, nullptr);
    }

    engine->envLod = std::clamp(engine->envLod, kMinEnvLod, static_cast<float>(engine->envHdrMipLevels > 0 ? engine->envHdrMipLevels - 1 : 0));
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
    engine->envLod = std::clamp(engine->envLod + delta, kMinEnvLod, static_cast<float>(engine->envHdrMipLevels > 0 ? engine->envHdrMipLevels - 1 : 0));
    LOG_INFO("runtime", "Env LOD: %.1f", engine->envLod);
}
