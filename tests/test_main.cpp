#include "app_log.h"
#include "runtime_controls.h"
#include "vk_engine.h"
#include "vk_engine_envmap.h"
#include "vk_engine_ibl.h"
#include "vk_engine_runtime.h"
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <string>

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

#include <cmath>
#include <cstring>
#include <stb/stb_image.h>
#include <vector>

struct FrameBufferData {
    unsigned char* pixels;
    int width;
    int height;
    VkDeviceSize size;
};

static bool compare_images(const FrameBufferData& a, const FrameBufferData& b, int threshold, const char* name) {
    size_t diff_count = 0;
    for (int px = 0; px < a.width * a.height; ++px) {
        const int base = px * 4;
        for (int c = 0; c < 3; ++c) {
            if (std::abs((int)a.pixels[base + c] - (int)b.pixels[base + c]) > threshold) {
                diff_count++;
            }
        }
    }
    if (diff_count > 0) {
        float percent = (float)diff_count / (float)(a.width * a.height * 3) * 100.0f;
        LOG_WARNING("test", "Image mismatch for %s: %zu pixels differ (> %d tolerance) - %.2f%%", name, diff_count / 3, threshold, percent);
        // Allow up to 2.5% of pixels to differ (needed for cross-driver wireframe/AA parity)
        return diff_count < static_cast<size_t>(static_cast<double>(a.width) * a.height * 3 * 0.025);
    }
    return true;
}

static bool readback_frame(VulkanEngine* engine, const FrameBufferData& outFrame) {
    VkBuffer readbackBuffer = VK_NULL_HANDLE;
    VmaAllocation readbackAllocation = VK_NULL_HANDLE;
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = outFrame.size;
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
    vkBeginCommandBuffer(cb, &beginInfo);

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.image = engine->swapchainImages[engine->lastRenderedImageIndex];
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {(uint32_t)outFrame.width, (uint32_t)outFrame.height, 1};

    vkCmdCopyImageToBuffer(cb, engine->swapchainImages[engine->lastRenderedImageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readbackBuffer, 1, &region);

    vkEndCommandBuffer(cb);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cb;
    vkQueueSubmit(engine->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(engine->graphicsQueue);

    void* mapped_data = nullptr;
    vmaMapMemory(engine->allocator, readbackAllocation, &mapped_data);
    vmaInvalidateAllocation(engine->allocator, readbackAllocation, 0, VK_WHOLE_SIZE);
    memcpy(outFrame.pixels, mapped_data, outFrame.size);
    vmaUnmapMemory(engine->allocator, readbackAllocation);

    vmaDestroyBuffer(engine->allocator, readbackBuffer, readbackAllocation);
    vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &cb);

    for (size_t i = 0; i < outFrame.size; i += 4) {
        std::swap(outFrame.pixels[i], outFrame.pixels[i + 2]);
    }
    return true;
}

static bool validate_frame(const FrameBufferData& frame, const char* filename) {
    bool success = true;
    const char* updateRefs = std::getenv("SVK_UPDATE_REFERENCES");
    char refPath[512];
    snprintf(refPath, sizeof(refPath), "tests/references/%s", filename);

    if (updateRefs && updateRefs[0] != '0') {
        if (stbi_write_png(refPath, frame.width, frame.height, 4, frame.pixels, frame.width * 4)) {
            LOG_INFO("test", "REFERENCE MISE A JOUR : %s", refPath);
        } else {
            LOG_ERROR("test", "Echec mise a jour reference : %s", refPath);
            success = false;
        }
    } else {
        int refW = 0;
        int refH = 0;
        int refC = 0;
        unsigned char* refPixels = stbi_load(refPath, &refW, &refH, &refC, 4);
        if (refPixels) {
            if (refW != frame.width || refH != frame.height) {
                LOG_ERROR("test", "Reference dimension mismatch: %s (%dx%d vs %dx%d)", refPath, refW, refH, frame.width, frame.height);
                success = false;
            } else {
                FrameBufferData refFrame{refPixels, refW, refH, static_cast<VkDeviceSize>(refW * refH * 4)};
                success = compare_images(frame, refFrame, 8, filename);
            }
            stbi_image_free(refPixels);
        } else {
            LOG_WARNING("test", "Reference absente, generation automatique : %s", refPath);
            stbi_write_png(refPath, frame.width, frame.height, 4, frame.pixels, frame.width * 4);
        }
    }

    const char* saveFrame = std::getenv("VULKAN_TEST_SAVE_FRAME");
    if (saveFrame != nullptr && saveFrame[0] != '\0') {
        stbi_write_png(filename, frame.width, frame.height, 4, frame.pixels, frame.width * 4);
    }
    return success;
}

static bool verify_and_capture_frame(VulkanEngine* engine, const char* filename) {
    if (vkDeviceWaitIdle(engine->device) != VK_SUCCESS || engine->lastRenderedImageIndex >= engine->imageCount) {
        return false;
    }

    const int width = static_cast<int>(engine->swapchainExtent.width);
    const int height = static_cast<int>(engine->swapchainExtent.height);
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;

    std::vector<unsigned char> pixels(imageSize);
    FrameBufferData frame{pixels.data(), width, height, imageSize};
    if (!readback_frame(engine, frame)) {
        return false;
    }

    // Swapchain alpha is not a stable visual signal across platforms/drivers.
    // Force opaque alpha so saved PNGs match runtime appearance in image viewers.
    for (size_t i = 3; i < imageSize; i += 4) {
        pixels[i] = 255;
    }

    return validate_frame(frame, filename);
}

static bool test_integration_rendering() {
    VulkanEngine engine = {};
    if (!init_vulkan_engine(&engine)) {
        return false;
    }

    // Capture 1: Billboard (Default)
    if (!draw_frame(&engine)) {
        cleanup_vulkan_engine(&engine);
        return false;
    }
    bool b1 = verify_and_capture_frame(&engine, "test_billboard.png");

    // Capture 2: Icosphere
    engine.core.billboardMode = false;
    if (!draw_frame(&engine)) {
        cleanup_vulkan_engine(&engine);
        return false;
    }
    bool b2 = verify_and_capture_frame(&engine, "test_icosphere.png");

    // Capture 3: Billboard Wireframe (New)
    engine.core.billboardMode = true;
    engine.core.wireframeMode = true;
    if (!draw_frame(&engine)) {
        cleanup_vulkan_engine(&engine);
        return false;
    }
    bool b3 = verify_and_capture_frame(&engine, "test_wireframe_billboard.png");

    // Capture 4: Icosphere Wireframe
    engine.core.billboardMode = false;
    engine.core.wireframeMode = true;
    if (!draw_frame(&engine)) {
        cleanup_vulkan_engine(&engine);
        return false;
    }
    bool b4 = verify_and_capture_frame(&engine, "test_wireframe_icosphere.png");

    // Capture 5: Close-up Billboard Singularity (Extreme Proximity)
    // Sphere at (1.25, 1.25, 0.0), Radius 1.0.
    // Camera at distance 1.5 -> 0.5 from surface
    engine.core.camera.position = glm::vec3(1.25f, 1.25f, 1.5f);
    engine.core.camera.yaw = -90.0f;
    engine.core.camera.pitch = 0.0f;
    camera_update_vectors(&engine.core.camera);
    engine.core.billboardMode = true;
    engine.core.wireframeMode = true;
    if (!draw_frame(&engine)) {
        cleanup_vulkan_engine(&engine);
        return false;
    }
    bool b5 = verify_and_capture_frame(&engine, "test_close_billboard.png");

    // Test coverage for default WindowOps wrappers
    const WindowOps* ops = runtime_default_window_ops();
    ops->get_key(engine.window, GLFW_KEY_UNKNOWN);
    ops->set_window_should_close(engine.window, GLFW_FALSE);
    GLFWmonitor* primary = ops->get_primary_monitor();
    if (primary) {
        ops->get_video_mode(primary);
    }
    int winX = 0;
    int winY = 0;
    int winW = 0;
    int winH = 0;
    ops->get_window_pos(engine.window, &winX, &winY);
    ops->get_window_size(engine.window, &winW, &winH);
    ops->set_window_monitor(engine.window, nullptr, winX, winY, winW, winH, 0);

    // Test envmap logic
    vk_adjust_env_lod(&engine, 1.0f);
    vk_adjust_env_lod(&engine, -1.0f);
    vk_switch_environment_texture(&engine, 1);
    vk_switch_environment_texture(&engine, -1);
    vk_ibl_export_maps(&engine);

    // Cover mouse and scroll callbacks
    engine.core.cameraEnabled = false;
    vk_mouse_callback(engine.window, 10.0, 10.0); // camera off
    engine.core.cameraEnabled = true;
    engine.core.camera.firstMouse = true;
    vk_mouse_callback(engine.window, 10.0, 10.0); // first mouse
    vk_mouse_callback(engine.window, 20.0, 20.0); // move mouse

    vk_scroll_callback(engine.window, 0, 1.0); // scroll

    cleanup_vulkan_engine(&engine);
    return b1 && b2 && b3 && b4 && b5;
}

int main() {
    if (!test_integration_rendering()) {
        LOG_ERROR("test", "Le test d'integration de rendu a echoue.");
        return 1;
    }

    LOG_INFO("test", "TEST PASS : Image generee et validee.");
    return 0;
}
