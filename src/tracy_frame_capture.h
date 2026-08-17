#ifndef TRACY_FRAME_CAPTURE_H
#define TRACY_FRAME_CAPTURE_H

#include "result.h"
#include <cstdint>
#include <vma/vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanEngine;

struct TracyFrameCaptureState {
    static constexpr uint32_t kWidth = 320;
    static constexpr uint32_t kHeight = 180;
    static constexpr uint32_t kInterval = 1;
    static constexpr uint32_t kPixelSize = 4; // RGBA8
    static constexpr VkDeviceSize kBufferSize = static_cast<VkDeviceSize>(kWidth) * kHeight * kPixelSize;

    VkImage downsampleImage{VK_NULL_HANDLE};
    VmaAllocation downsampleAllocation{VK_NULL_HANDLE};
    VkBuffer stagingBuffers[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VmaAllocation stagingAllocations[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    void* mappedPointers[2]{nullptr, nullptr};
    uint32_t currentBufferIdx{0};
    bool hasPendingFrame[2]{false, false};
    bool initialized{false};
};

GfxResult init_tracy_frame_capture(VulkanEngine* engine);
void record_tracy_frame_capture(VulkanEngine* engine, VkCommandBuffer cb, VkImage srcImage, VkExtent2D srcExtent, VkImageLayout currentLayout);
void emit_tracy_frame_capture(VulkanEngine* engine);
void cleanup_tracy_frame_capture(VulkanEngine* engine);

#endif // TRACY_FRAME_CAPTURE_H
