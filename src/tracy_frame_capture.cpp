#include "tracy_frame_capture.h"
#include "app_log.h"
#include "tracy_client.h"
#include "vk_engine.h"

GfxResult init_tracy_frame_capture(VulkanEngine* engine) {
    if (!engine) {
        return GfxResult::ErrorInvalidState;
    }
    auto& state = engine->tracyCapture;
    if (state.initialized) {
        return GfxResult::Success;
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {TracyFrameCaptureState::kWidth, TracyFrameCaptureState::kHeight, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(engine->ctx.allocator, &imageInfo, &allocInfo, &state.downsampleImage, &state.downsampleAllocation, nullptr) != VK_SUCCESS) {
        LOG_ERROR("tracy", "Failed to create Tracy thumbnail downsample image");
        return GfxResult::ErrorInitializationFailed;
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = TracyFrameCaptureState::kBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    for (int i = 0; i < 2; ++i) {
        VmaAllocationInfo allocationInfo{};
        if (vmaCreateBuffer(engine->ctx.allocator, &bufferInfo, &stagingAllocInfo, &state.stagingBuffers[i], &state.stagingAllocations[i], &allocationInfo) !=
            VK_SUCCESS) {
            LOG_ERROR("tracy", "Failed to create Tracy thumbnail staging buffer %d", i);
            cleanup_tracy_frame_capture(engine);
            return GfxResult::ErrorInitializationFailed;
        }
        state.mappedPointers[i] = allocationInfo.pMappedData;
        state.hasPendingFrame[i] = false;
    }

    state.currentBufferIdx = 0;
    state.initialized = true;
    LOG_INFO("tracy", "Tracy frame thumbnail capture initialized (160x120 RGBA, double-buffered).");
    return GfxResult::Success;
}

void record_tracy_frame_capture(VulkanEngine* engine, VkCommandBuffer cb, VkImage srcImage, VkExtent2D srcExtent, VkImageLayout currentLayout) {
    if (!engine || !engine->tracyCapture.initialized || srcImage == VK_NULL_HANDLE || cb == VK_NULL_HANDLE) {
        return;
    }

#ifdef TRACY_ENABLE
    if (!TracyIsConnected || (engine->totalFramesRendered % TracyFrameCaptureState::kInterval != 0)) {
        return;
    }

    auto& state = engine->tracyCapture;
    uint32_t writeIdx = state.currentBufferIdx;

    VkImageMemoryBarrier barriers[2]{};
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].oldLayout = currentLayout;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].image = srcImage;
    barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].srcAccessMask = 0;
    barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].image = state.downsampleImage;
    barriers[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);

    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {static_cast<int32_t>(srcExtent.width), static_cast<int32_t>(srcExtent.height), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {static_cast<int32_t>(TracyFrameCaptureState::kWidth), static_cast<int32_t>(TracyFrameCaptureState::kHeight), 1};

    vkCmdBlitImage(cb, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, state.downsampleImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].newLayout = currentLayout;
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;

    barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2,
                         barriers);

    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = TracyFrameCaptureState::kWidth;
    copyRegion.bufferImageHeight = TracyFrameCaptureState::kHeight;
    copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.imageOffset = {0, 0, 0};
    copyRegion.imageExtent = {TracyFrameCaptureState::kWidth, TracyFrameCaptureState::kHeight, 1};

    vkCmdCopyImageToBuffer(cb, state.downsampleImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, state.stagingBuffers[writeIdx], 1, &copyRegion);

    state.hasPendingFrame[writeIdx] = true;
    state.currentBufferIdx = (writeIdx + 1) % 2;
#else
    (void)srcExtent;
    (void)currentLayout;
#endif
}

void emit_tracy_frame_capture(VulkanEngine* engine) {
    if (!engine || !engine->tracyCapture.initialized) {
        return;
    }

#ifdef TRACY_ENABLE
    auto& state = engine->tracyCapture;
    uint32_t readIdx = state.currentBufferIdx;

    if (state.hasPendingFrame[readIdx] && state.mappedPointers[readIdx] != nullptr) {
        vmaInvalidateAllocation(engine->ctx.allocator, state.stagingAllocations[readIdx], 0, VK_WHOLE_SIZE);
        SVK_TRACY_FRAME_IMAGE(state.mappedPointers[readIdx], TracyFrameCaptureState::kWidth, TracyFrameCaptureState::kHeight, 1, 0);
        state.hasPendingFrame[readIdx] = false;
    }
#endif
}

void cleanup_tracy_frame_capture(VulkanEngine* engine) {
    if (!engine || !engine->tracyCapture.initialized) {
        return;
    }
    auto& state = engine->tracyCapture;

    for (int i = 0; i < 2; ++i) {
        if (state.stagingBuffers[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(engine->ctx.allocator, state.stagingBuffers[i], state.stagingAllocations[i]);
            state.stagingBuffers[i] = VK_NULL_HANDLE;
            state.stagingAllocations[i] = VK_NULL_HANDLE;
            state.mappedPointers[i] = nullptr;
            state.hasPendingFrame[i] = false;
        }
    }

    if (state.downsampleImage != VK_NULL_HANDLE) {
        vmaDestroyImage(engine->ctx.allocator, state.downsampleImage, state.downsampleAllocation);
        state.downsampleImage = VK_NULL_HANDLE;
        state.downsampleAllocation = VK_NULL_HANDLE;
    }

    state.initialized = false;
    LOG_INFO("tracy", "Tracy frame thumbnail capture cleaned up.");
}
