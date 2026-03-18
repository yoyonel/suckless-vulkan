#include "app_log.h"
#include "vk_engine.h"
#include <cstdlib>

// On réduit au silence les warnings de la lib tierce pour le compilateur
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#include <stb/stb_image_write.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

static bool verify_and_capture_frame(VulkanEngine* engine, const char* debugFilename) {
    if (vkDeviceWaitIdle(engine->device) != VK_SUCCESS) {
        return false;
    }

    if (engine->lastRenderedImageIndex >= engine->imageCount) {
        return false;
    }

    const VkImage srcImage = engine->swapchainImages[engine->lastRenderedImageIndex];
    const int width = static_cast<int>(engine->swapchainExtent.width);
    const int height = static_cast<int>(engine->swapchainExtent.height);
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;

    VkBuffer readbackBuffer = VK_NULL_HANDLE;
    VmaAllocation readbackAllocation = VK_NULL_HANDLE;
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

    if (vmaCreateBuffer(engine->allocator, &bufferInfo, &allocInfo, &readbackBuffer, &readbackAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    VkCommandBufferAllocateInfo cmdAlloc = {};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.commandPool = engine->commandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;

    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(engine->device, &cmdAlloc, &cb) != VK_SUCCESS) {
        vmaDestroyBuffer(engine->allocator, readbackBuffer, readbackAllocation);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cb, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &cb);
        vmaDestroyBuffer(engine->allocator, readbackBuffer, readbackAllocation);
        return false;
    }

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.image = srcImage;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {(uint32_t)width, (uint32_t)height, 1};

    vkCmdCopyImageToBuffer(cb, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readbackBuffer, 1, &region);

    if (vkEndCommandBuffer(cb) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &cb);
        vmaDestroyBuffer(engine->allocator, readbackBuffer, readbackAllocation);
        return false;
    }

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cb;
    if (vkQueueSubmit(engine->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS || vkQueueWaitIdle(engine->graphicsQueue) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &cb);
        vmaDestroyBuffer(engine->allocator, readbackBuffer, readbackAllocation);
        return false;
    }

    void* data = nullptr;
    if (vmaMapMemory(engine->allocator, readbackAllocation, &data) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &cb);
        vmaDestroyBuffer(engine->allocator, readbackBuffer, readbackAllocation);
        return false;
    }
    if (vmaInvalidateAllocation(engine->allocator, readbackAllocation, 0, VK_WHOLE_SIZE) != VK_SUCCESS) {
        vmaUnmapMemory(engine->allocator, readbackAllocation);
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &cb);
        vmaDestroyBuffer(engine->allocator, readbackBuffer, readbackAllocation);
        return false;
    }

    unsigned char* pixels = static_cast<unsigned char*>(data);

    for (size_t i = 0; i < imageSize; i += 4) {
        unsigned char tmp = pixels[i];
        pixels[i] = pixels[i + 2];
        pixels[i + 2] = tmp;
    }

    bool has_content = false;
    for (size_t i = 0; i < imageSize; i++) {
        if (pixels[i] > 0) {
            has_content = true;
            break;
        }
    }

    const char* saveFrame = std::getenv("VULKAN_TEST_SAVE_FRAME");
    if (saveFrame != nullptr && saveFrame[0] != '\0') {
        // NOLINTNEXTLINE(clang-analyzer-optin.portability.UnixAPI)
        if (stbi_write_png(debugFilename, width, height, 4, data, width * 4)) {
            LOG_INFO("test", "Frame sauvegardee sous : %s", debugFilename);
        } else {
            LOG_WARNING("test", "Impossible de sauvegarder la frame de debug (%s).", debugFilename);
        }
    }

    vmaUnmapMemory(engine->allocator, readbackAllocation);
    vmaDestroyBuffer(engine->allocator, readbackBuffer, readbackAllocation);
    vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &cb);

    return has_content;
}

static bool test_integration_rendering() {
    VulkanEngine engine = {};
    if (!init_vulkan_engine(&engine)) {
        return false;
    }

    if (!draw_frame(&engine)) {
        cleanup_vulkan_engine(&engine);
        return false;
    }

    const bool success = verify_and_capture_frame(&engine, "test_output.png");

    cleanup_vulkan_engine(&engine);
    return success;
}

int main() {
    if (!test_integration_rendering()) {
        LOG_ERROR("test", "Le test d'integration de rendu a echoue.");
        return 1;
    }

    LOG_INFO("test", "TEST PASS : Image generee et validee.");
    return 0;
}
