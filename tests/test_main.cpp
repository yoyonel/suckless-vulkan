#include "app_log.h"
#include "engine_config.h"
#include "rhi/vulkan_command_list.h"
#include "rhi/vulkan_rhi.h"
#include "runtime_controls.h"
#include "vk_engine.h"
#include "vk_engine_envmap.h"
#include "vk_engine_ibl.h"
#include "vk_engine_init.h"
#include "vk_engine_runtime.h"
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <string>

#include <stb/stb_image_write.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <stb/stb_image.h>
#include <thread>
#include <vector>

struct FrameBufferData {
    unsigned char* pixels;
    int width;
    int height;
    VkDeviceSize size;
};

static bool compare_images(const FrameBufferData& a, const FrameBufferData& b, int threshold, const char* name) {
    size_t diff_count = 0;
    double sse = 0.0;
    unsigned char* diff_pixels = new unsigned char[static_cast<size_t>(a.width) * static_cast<size_t>(a.height) * 4];
    for (int px = 0; px < a.width * a.height; ++px) {
        const int base = px * 4;
        bool is_diff = false;
        for (int c = 0; c < 3; ++c) {
            int diff = (int)a.pixels[base + c] - (int)b.pixels[base + c];
            sse += (double)(diff * diff);
            if (std::abs(diff) > threshold) {
                is_diff = true;
                diff_count++;
            }
        }
        if (is_diff) {
            diff_pixels[base] = 255;
            diff_pixels[base + 1] = 0;
            diff_pixels[base + 2] = 255;
            diff_pixels[base + 3] = 255;
        } else {
            diff_pixels[base] = 0;
            diff_pixels[base + 1] = 0;
            diff_pixels[base + 2] = 0;
            diff_pixels[base + 3] = 255;
        }
    }
    double mse = sse / (a.width * a.height * 3.0);
    double rmse = std::sqrt(mse);
    bool success = true;

    if (diff_count > 0 || rmse > 0.0) {
        float percent = (float)diff_count / (float)(a.width * a.height * 3) * 100.0f;
        LOG_INFO("test", "Comparison %s: %zu diffs (%.2f%%) | RMSE: %.3f", name, diff_count / 3, percent, rmse);

        const double MAX_RMSE = 2.0;
        const float MAX_PERCENT = 2.5f;
        success = (rmse <= MAX_RMSE) || (percent <= MAX_PERCENT);
        if (!success) {
            LOG_WARNING("test", "Image mismatch for %s exceeds thresholds (RMSE: %.3f > %.1f AND Diff: %.2f%% > %.1f%%)", name, rmse, MAX_RMSE, percent,
                        MAX_PERCENT);
        }
    }

    if (!success) {
        char path_out[256];
        char path_diff[256];
        snprintf(path_out, sizeof(path_out), "tests/failures/failed_%s", name);
        snprintf(path_diff, sizeof(path_diff), "tests/failures/diff_%s", name);
        stbi_write_png(path_out, a.width, a.height, 4, a.pixels, a.width * 4);
        stbi_write_png(path_diff, a.width, a.height, 4, diff_pixels, a.width * 4);
        LOG_WARNING("test", "Wrote failure outputs to %s and %s", path_out, path_diff);
    }

    delete[] diff_pixels;
    return success;
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

    if (vmaCreateBuffer(engine->ctx.allocator, &bufferInfo, &allocInfo, &readbackBuffer, &readbackAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    VkCommandBufferAllocateInfo cmdAlloc = {};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.commandPool = engine->ctx.commandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;

    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(engine->ctx.device, &cmdAlloc, &cb) != VK_SUCCESS) {
        vmaDestroyBuffer(engine->ctx.allocator, readbackBuffer, readbackAllocation);
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
    barrier.image = engine->swapchainMgr.swapchainImages[engine->lastRenderedImageIndex];
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {(uint32_t)outFrame.width, (uint32_t)outFrame.height, 1};

    vkCmdCopyImageToBuffer(cb, engine->swapchainMgr.swapchainImages[engine->lastRenderedImageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readbackBuffer, 1,
                           &region);

    vkEndCommandBuffer(cb);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cb;
    vkQueueSubmit(engine->ctx.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(engine->ctx.graphicsQueue);

    void* mapped_data = nullptr;
    vmaMapMemory(engine->ctx.allocator, readbackAllocation, &mapped_data);
    vmaInvalidateAllocation(engine->ctx.allocator, readbackAllocation, 0, VK_WHOLE_SIZE);
    memcpy(outFrame.pixels, mapped_data, outFrame.size);
    vmaUnmapMemory(engine->ctx.allocator, readbackAllocation);

    vmaDestroyBuffer(engine->ctx.allocator, readbackBuffer, readbackAllocation);
    vkFreeCommandBuffers(engine->ctx.device, engine->ctx.commandPool, 1, &cb);

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
            LOG_ERROR("test", "Reference absente : %s. Utilisez SVK_UPDATE_REFERENCES=1 pour la generer.", refPath);
            success = false;
        }
    }

    const char* saveFrame = std::getenv("VULKAN_TEST_SAVE_FRAME");
    if (saveFrame != nullptr && saveFrame[0] != '\0') {
        stbi_write_png(filename, frame.width, frame.height, 4, frame.pixels, frame.width * 4);
    }
    return success;
}

static bool verify_and_capture_frame(VulkanEngine* engine, const char* filename) {
    if (vkDeviceWaitIdle(engine->ctx.device) != VK_SUCCESS || engine->lastRenderedImageIndex >= engine->swapchainMgr.imageCount) {
        return false;
    }

    const int width = static_cast<int>(engine->swapchainMgr.swapchainExtent.width);
    const int height = static_cast<int>(engine->swapchainMgr.swapchainExtent.height);
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

#include "rhi/render_graph.h"

static bool test_render_graph_vulkan(VulkanEngine* engine) {
    bool success = true;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {128, 128, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImage img1;
    VkImage img2;
    VmaAllocation alloc1;
    VmaAllocation alloc2;
    if (vmaCreateImage(engine->ctx.allocator, &imageInfo, &allocInfo, &img1, &alloc1, nullptr) != VK_SUCCESS)
        return false;
    if (vmaCreateImage(engine->ctx.allocator, &imageInfo, &allocInfo, &img2, &alloc2, nullptr) != VK_SUCCESS)
        return false;

    rhi::RenderGraph graph;
    auto vImg1 = graph.CreateVirtualImage("img1");
    auto vImg2 = graph.CreateVirtualImage("img2");

    graph.BindPhysicalResource(vImg1, img1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    graph.BindPhysicalResource(vImg2, img2, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

    graph.AddPass("Pass1", {}, {{vImg1, rhi::ResourceState::RenderTarget}}, [](VkCommandBuffer) {});
    graph.AddPass("Pass2", {{vImg1, rhi::ResourceState::ShaderRead}}, {{vImg2, rhi::ResourceState::RenderTarget}}, [](VkCommandBuffer) {});

    if (!graph.Compile()) {
        LOG_ERROR("test", "Vulkan RenderGraph failed to compile");
        success = false;
    }

    VkCommandBufferAllocateInfo cmdAlloc = {};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.commandPool = engine->ctx.commandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(engine->ctx.device, &cmdAlloc, &cb);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &beginInfo);

    graph.Execute(cb);

    vkEndCommandBuffer(cb);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cb;
    vkQueueSubmit(engine->ctx.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(engine->ctx.graphicsQueue);

    vkFreeCommandBuffers(engine->ctx.device, engine->ctx.commandPool, 1, &cb);
    vmaDestroyImage(engine->ctx.allocator, img1, alloc1);
    vmaDestroyImage(engine->ctx.allocator, img2, alloc2);

    if (success) {
        LOG_INFO("test", "Vulkan RenderGraph executed successfully (Validation Layers should be clean)");
    }
    return success;
}

static std::vector<uint32_t> load_spv(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f)
        return {};
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint32_t> buf(size / sizeof(uint32_t));
    if (fread(buf.data(), 1, size, f) != static_cast<size_t>(size)) {
        fclose(f);
        return {};
    }
    fclose(f);
    return buf;
}

static bool test_subpass_fusion_standalone(VulkanEngine* engine) {
    VulkanRHI* vkRhi = static_cast<VulkanRHI*>(engine->appState->rhi);

    VkAttachmentDescription attachments[3] = {};
    // Attachment 0: Intermediate HDR Color (On-chip, DONT_CARE store)
    attachments[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Attachment 1: Depth Buffer (On-chip, DONT_CARE store)
    attachments[1].format = engine->swapchainMgr.depthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Attachment 2: Swapchain Output (Saved to DRAM for presentation)
    attachments[2].format = engine->swapchainMgr.swapchainImageFormat;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef0{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef0{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkAttachmentReference inputRef1{0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkAttachmentReference colorRef1{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpasses[2] = {};
    // Subpass 0: Forward
    subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[0].colorAttachmentCount = 1;
    subpasses[0].pColorAttachments = &colorRef0;
    subpasses[0].pDepthStencilAttachment = &depthRef0;

    // Subpass 1: PostProcess
    subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[1].inputAttachmentCount = 1;
    subpasses[1].pInputAttachments = &inputRef1;
    subpasses[1].colorAttachmentCount = 1;
    subpasses[1].pColorAttachments = &colorRef1;

    VkSubpassDependency dependencies[2] = {};
    // External -> Subpass 0
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = 0;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    // Subpass 0 -> Subpass 1 (On-Chip)
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = 1;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 3;
    rpInfo.pAttachments = attachments;
    rpInfo.subpassCount = 2;
    rpInfo.pSubpasses = subpasses;
    rpInfo.dependencyCount = 2;
    rpInfo.pDependencies = dependencies;

    VkRenderPass fusedRenderPass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(engine->ctx.device, &rpInfo, nullptr, &fusedRenderPass) != VK_SUCCESS)
        return false;

    VkImageView depthView = vkRhi->GetVkImageView(engine->swapchainMgr.depthImage);
    VkImageView colorView = vkRhi->GetVkImageView(engine->swapchainMgr.colorAttachment);
    VkImageView fbAtt[] = {colorView, depthView, engine->swapchainMgr.swapchainImageViews[0]};

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = fusedRenderPass;
    fbInfo.attachmentCount = 3;
    fbInfo.pAttachments = fbAtt;
    fbInfo.width = engine->swapchainMgr.swapchainExtent.width;
    fbInfo.height = engine->swapchainMgr.swapchainExtent.height;
    fbInfo.layers = 1;

    VkFramebuffer fusedFramebuffer = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(engine->ctx.device, &fbInfo, nullptr, &fusedFramebuffer) != VK_SUCCESS) {
        vkDestroyRenderPass(engine->ctx.device, fusedRenderPass, nullptr);
        return false;
    }

    DescriptorSetLayoutBinding ppBinding = {0, DescriptorType::InputAttachment, 1, ShaderStage::Fragment};
    DescriptorLayoutDesc ppLayoutDesc{&ppBinding, 1};
    DescriptorLayoutHandle ppLayoutHandle = vkRhi->CreateDescriptorLayout(ppLayoutDesc, "Test_PP_Layout");

    DescriptorLayoutHandle ppHandles[] = {ppLayoutHandle};
    PipelineLayoutDesc pipeLayoutDesc{ppHandles, 1, nullptr, 0};
    PipelineLayoutHandle ppPipelineLayout = vkRhi->CreatePipelineLayout(pipeLayoutDesc, "Test_PP_PipeLayout");

    auto vertSpv = load_spv("shaders/postprocess_vert.spv");
    auto fragSpv = load_spv("shaders/postprocess_subpass_frag.spv");

    GraphicsPipelineDesc pipeDesc{};
    pipeDesc.layout = ppPipelineLayout;
    pipeDesc.renderPass = fusedRenderPass;
    pipeDesc.subpass = 1;
    pipeDesc.vertexShaderCode = vertSpv.data();
    pipeDesc.vertexShaderSize = vertSpv.size() * sizeof(uint32_t);
    pipeDesc.fragmentShaderCode = fragSpv.data();
    pipeDesc.fragmentShaderSize = fragSpv.size() * sizeof(uint32_t);
    pipeDesc.depthTestEnable = false;
    pipeDesc.depthWriteEnable = false;
    pipeDesc.cullMode = CullMode::None;
    pipeDesc.debugName = "Test_PostProcess_Subpass1";

    PipelineHandle ppPipeline = vkRhi->CreateGraphicsPipeline(pipeDesc);

    DescriptorPoolSize poolSizes[] = {{DescriptorType::InputAttachment, 1}};
    DescriptorPoolDesc poolDesc{poolSizes, 1, 1};
    DescriptorPoolHandle ppPool = vkRhi->CreateDescriptorPool(poolDesc, "Test_PP_Pool");

    DescriptorSetHandle ppSet = INVALID_HANDLE;
    DescriptorSetAllocateDesc allocDesc{};
    allocDesc.pool = ppPool;
    allocDesc.setCount = 1;
    allocDesc.layouts = &ppLayoutHandle;
    (void)vkRhi->AllocateDescriptorSets(allocDesc, &ppSet);

    DescriptorImageInfo imgInfo{};
    imgInfo.sampler = INVALID_HANDLE;
    imgInfo.imageView = INVALID_HANDLE;
    imgInfo.texture = engine->swapchainMgr.colorAttachment;
    imgInfo.imageLayout = TextureLayout::ShaderReadOnlyOptimal;

    WriteDescriptorSet write{};
    write.dstSet = ppSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = DescriptorType::InputAttachment;
    write.pImageInfo = &imgInfo;
    vkRhi->UpdateDescriptorSets(1, &write);

    VkCommandBufferAllocateInfo cbAlloc{};
    cbAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAlloc.commandPool = engine->ctx.commandPool;
    cbAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(engine->ctx.device, &cbAlloc, &cb);

    VkCommandBufferBeginInfo cbBegin{};
    cbBegin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &cbBegin);

    VkClearValue cl[3] = {};
    cl[0].color = {{0.05f, 0.05f, 0.2f, 1.0f}};
    cl[1].depthStencil = {1.0f, 0};
    cl[2].color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = fusedRenderPass;
    rpBegin.framebuffer = fusedFramebuffer;
    rpBegin.renderArea.extent = engine->swapchainMgr.swapchainExtent;
    rpBegin.clearValueCount = 3;
    rpBegin.pClearValues = cl;

    vkCmdBeginRenderPass(cb, &rpBegin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    vkCmdExecuteCommands(cb, 1, &engine->secondaryForwardCb[0]);
    vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);

    VulkanCommandList cmdListWrapper(vkRhi, cb);
    cmdListWrapper.SetViewport(0.0f, 0.0f, static_cast<float>(engine->swapchainMgr.swapchainExtent.width),
                               static_cast<float>(engine->swapchainMgr.swapchainExtent.height), 0.0f, 1.0f);
    cmdListWrapper.SetScissor(0, 0, engine->swapchainMgr.swapchainExtent.width, engine->swapchainMgr.swapchainExtent.height);
    cmdListWrapper.BindPipeline(ppPipeline);
    cmdListWrapper.BindDescriptorSets(ppPipelineLayout, 0, 1, &ppSet, false);
    cmdListWrapper.Draw(3, 1, 0, 0);

    vkCmdEndRenderPass(cb);
    vkEndCommandBuffer(cb);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cb;
    vkQueueSubmit(engine->ctx.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(engine->ctx.graphicsQueue);

    engine->lastRenderedImageIndex = 0;
    bool success = verify_and_capture_frame(engine, "test_icosphere.png");

    vkFreeCommandBuffers(engine->ctx.device, engine->ctx.commandPool, 1, &cb);
    vkRhi->DestroyPipeline(ppPipeline);
    vkRhi->DestroyPipelineLayout(ppPipelineLayout);
    vkRhi->DestroyDescriptorPool(ppPool);
    vkRhi->DestroyDescriptorLayout(ppLayoutHandle);
    vkDestroyFramebuffer(engine->ctx.device, fusedFramebuffer, nullptr);
    vkDestroyRenderPass(engine->ctx.device, fusedRenderPass, nullptr);

    if (success) {
        LOG_INFO("test", "Subpass Fusion standalone golden image test PASSED");
    } else {
        LOG_ERROR("test", "Subpass Fusion standalone golden image test FAILED");
    }
    return success;
}

static bool test_swapchain_resize(VulkanEngine* engine) {
    glfwSetWindowSize(engine->appState->window, 1280, 720);
    int fbW = 0;
    int fbH = 0;
    glfwGetFramebufferSize(engine->appState->window, &fbW, &fbH);
    for (int retry = 0; retry < 50 && (fbW != 1280 || fbH != 720); ++retry) {
        glfwPollEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        glfwGetFramebufferSize(engine->appState->window, &fbW, &fbH);
    }

    if (vk_recreate_swapchain(engine) != GfxResult::Success) {
        LOG_ERROR("test", "Failed to recreate swapchain after resize to 1280x720");
        return false;
    }
    if (draw_frame(engine) != GfxResult::Success || draw_frame(engine) != GfxResult::Success) {
        LOG_ERROR("test", "Failed to render frame after resize to 1280x720");
        return false;
    }
    if (engine->swapchainMgr.swapchainExtent.width != 1280 || engine->swapchainMgr.swapchainExtent.height != 720) {
        LOG_ERROR("test", "Swapchain extent mismatch: %dx%d != 1280x720", engine->swapchainMgr.swapchainExtent.width,
                  engine->swapchainMgr.swapchainExtent.height);
        return false;
    }

    const size_t w = static_cast<size_t>(engine->swapchainMgr.swapchainExtent.width);
    const size_t h = static_cast<size_t>(engine->swapchainMgr.swapchainExtent.height);
    std::vector<unsigned char> px(w * h * 4U);
    FrameBufferData fbd{px.data(), static_cast<int>(w), static_cast<int>(h), static_cast<VkDeviceSize>(w * h * 4U)};
    if (!readback_frame(engine, fbd)) {
        LOG_ERROR("test", "Failed readback after resize to 1280x720");
        return false;
    }

    size_t sampleIdx = ((680U * w) + 1200U) * 4U;
    bool hasContent = (px[sampleIdx] != 0 || px[sampleIdx + 1] != 0 || px[sampleIdx + 2] != 0);
    if (!hasContent) {
        LOG_ERROR("test", "Bottom-right region is unrendered after resize (px=%u,%u,%u,%u)", px[sampleIdx], px[sampleIdx + 1], px[sampleIdx + 2],
                  px[sampleIdx + 3]);
        return false;
    }

    glfwSetWindowSize(engine->appState->window, 1024, 768);
    fbW = 0;
    fbH = 0;
    glfwGetFramebufferSize(engine->appState->window, &fbW, &fbH);
    for (int retry = 0; retry < 50 && (fbW != 1024 || fbH != 768); ++retry) {
        glfwPollEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        glfwGetFramebufferSize(engine->appState->window, &fbW, &fbH);
    }

    if (vk_recreate_swapchain(engine) != GfxResult::Success) {
        LOG_ERROR("test", "Failed to recreate swapchain back to 1024x768");
        return false;
    }
    if (draw_frame(engine) != GfxResult::Success || draw_frame(engine) != GfxResult::Success) {
        LOG_ERROR("test", "Failed to render frame back to 1024x768");
        return false;
    }
    return true;
}

static bool test_autoexposure_synthetic(VulkanEngine* engine) {
    VulkanRHI* vkRhi = static_cast<VulkanRHI*>(engine->appState->rhi);

    // 1. Create a 64x64 RGBA32F synthetic texture with known luminances:
    // 2048 pixels = 1.0 (Luminance = 1.0 -> Bin 32)
    // 2048 pixels = 0.0625 (Luminance = 0.0625 -> Bin 16)
    uint32_t texW = 64;
    uint32_t texH = 64;
    std::vector<float> testPixels(static_cast<size_t>(texW) * static_cast<size_t>(texH) * 4, 0.0f);
    for (uint32_t y = 0; y < texH; ++y) {
        for (uint32_t x = 0; x < texW; ++x) {
            uint32_t idx = ((y * texW) + x) * 4;
            float val = (y < texH / 2) ? 1.0f : 0.0625f;
            testPixels[idx + 0] = val;
            testPixels[idx + 1] = val;
            testPixels[idx + 2] = val;
            testPixels[idx + 3] = 1.0f;
        }
    }

    TextureHandle synthTex = vkRhi->CreateTexture(texW, texH, TextureFormat::RGBA32_SFLOAT, TextureUsage::Sampled, 1, "AE_Synth_Tex");
    if (synthTex == INVALID_HANDLE) {
        LOG_ERROR("test", "Failed to create synthetic texture for AE test");
        return false;
    }

    // Upload pixels to synthTex via staging buffer
    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = testPixels.size() * sizeof(float);
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    vmaCreateBuffer(engine->ctx.allocator, &bufInfo, &allocInfo, &stagingBuf, &stagingAlloc, nullptr);

    void* mapped = nullptr;
    vmaMapMemory(engine->ctx.allocator, stagingAlloc, &mapped);
    memcpy(mapped, testPixels.data(), bufInfo.size);
    vmaUnmapMemory(engine->ctx.allocator, stagingAlloc);

    VkCommandBuffer cb = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cbAi{};
    cbAi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAi.commandPool = engine->ctx.commandPool;
    cbAi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAi.commandBufferCount = 1;
    vkAllocateCommandBuffers(engine->ctx.device, &cbAi, &cb);

    VkCommandBufferBeginInfo cbBi{};
    cbBi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbBi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &cbBi);

    VkImage synthVkImg = vkRhi->GetVkImage(synthTex);

    // Transition synthTex: Undefined -> TransferDst
    VkImageMemoryBarrier tBarrier{};
    tBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    tBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    tBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    tBarrier.srcAccessMask = 0;
    tBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    tBarrier.image = synthVkImg;
    tBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &tBarrier);

    VkBufferImageCopy copyRegion{};
    copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.imageExtent = {texW, texH, 1};
    vkCmdCopyBufferToImage(cb, stagingBuf, synthVkImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    // Transition synthTex: TransferDst -> ShaderReadOnlyOptimal
    tBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    tBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    tBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    tBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &tBarrier);

    // Now run AutoExposure passes!
    engine->autoexposure.enabled = true;
    engine->autoexposure.debugOverlay = true;
    engine->autoexposure.RecordPasses(engine, cb, synthTex, texW, texH, 0.016f);

    // Prepare readback of debugHistogramBuffer to CPU
    VkBuffer readbackHistBuf = VK_NULL_HANDLE;
    VmaAllocation readbackHistAlloc = VK_NULL_HANDLE;
    VkBufferCreateInfo histReadInfo{};
    histReadInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    histReadInfo.size = 64 * sizeof(uint32_t);
    histReadInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo readAllocInfo{};
    readAllocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
    vmaCreateBuffer(engine->ctx.allocator, &histReadInfo, &readAllocInfo, &readbackHistBuf, &readbackHistAlloc, nullptr);

    VkBufferCopy copyHist{};
    copyHist.srcOffset = 0;
    copyHist.dstOffset = 0;
    copyHist.size = 64 * sizeof(uint32_t);
    vkCmdCopyBuffer(cb, vkRhi->GetVkBuffer(engine->autoexposure.debugHistogramBuffer.get()), readbackHistBuf, 1, &copyHist);

    vkEndCommandBuffer(cb);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cb;
    vkQueueSubmit(engine->ctx.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(engine->ctx.graphicsQueue);

    // Read back and inspect histogram bins!
    uint32_t readbackBins[64] = {0};
    void* mappedHist = nullptr;
    vmaMapMemory(engine->ctx.allocator, readbackHistAlloc, &mappedHist);
    vmaInvalidateAllocation(engine->ctx.allocator, readbackHistAlloc, 0, VK_WHOLE_SIZE);
    memcpy(readbackBins, mappedHist, sizeof(readbackBins));
    vmaUnmapMemory(engine->ctx.allocator, readbackHistAlloc);

    for (int i = 0; i < 64; ++i) {
        if (readbackBins[i] > 0) {
            LOG_INFO("test", "AutoExposure Hist Bin[%d] = %u", i, readbackBins[i]);
        }
    }

    // Cleanup
    vmaDestroyBuffer(engine->ctx.allocator, stagingBuf, stagingAlloc);
    vmaDestroyBuffer(engine->ctx.allocator, readbackHistBuf, readbackHistAlloc);
    vkFreeCommandBuffers(engine->ctx.device, engine->ctx.commandPool, 1, &cb);
    vkRhi->DestroyTexture(synthTex);

    bool ok = (readbackBins[16] == 2048 && readbackBins[32] == 2048);
    if (ok) {
        LOG_INFO("test", "AutoExposure synthetic GPU histogram test PASSED (Bin 16=2048, Bin 32=2048)");
    } else {
        LOG_ERROR("test", "AutoExposure synthetic GPU histogram test FAILED (Expected Bin 16=2048, Bin 32=2048, got Bin 16=%u, Bin 32=%u)", readbackBins[16],
                  readbackBins[32]);
    }
    return ok;
}

extern "C" IRHI* CreateRHI(EngineState*);
extern "C" void DestroyRHI(IRHI*);
static bool test_integration_rendering() {
    EngineState appState = {};

    arena_init(&appState.rhiArena, RHI_ARENA_CAPACITY_BYTES);
    core_engine_init(&appState.core);
    appState.core.cameraEnabled = false;

    if (glfwInit() != GLFW_TRUE) {
        return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    appState.window = glfwCreateWindow(1024, 768, "Test", NULL, NULL);
    if (!appState.window) {
        return false;
    }

    appState.rhi = CreateRHI(&appState);
    if (appState.rhi->Init() != RHIResult::Success) {
        return false;
    }
    VulkanEngine& engine = *static_cast<VulkanRHI*>(appState.rhi)->_engine;
    // init_vulkan_engine is called by Init()

    // Capture 1: Billboard (Default)
    engine.appState->core.render.bloomEnabled = false;
    if (draw_frame(&engine) != GfxResult::Success) {
        cleanup_vulkan_engine(&engine);
        return false;
    }
    bool b1 = verify_and_capture_frame(&engine, "test_billboard.png");

    // Capture 1b: Bloom Enabled on Default View
    engine.appState->core.render.bloomEnabled = true;
    engine.appState->core.render.bloomIntensity = 0.30f;
    if (draw_frame(&engine) != GfxResult::Success) {
        cleanup_vulkan_engine(&engine);
        return false;
    }
    bool b_bloom = verify_and_capture_frame(&engine, "test_bloom.png");

    // Capture 1c: Bloom Debug Final Map on Default View
    engine.appState->core.render.bloomDebugMode = 1;
    if (draw_frame(&engine) != GfxResult::Success) {
        cleanup_vulkan_engine(&engine);
        return false;
    }
    bool b_bloom_debug = verify_and_capture_frame(&engine, "test_bloom_debug.png");
    engine.appState->core.render.bloomDebugMode = 0;
    engine.appState->core.render.bloomEnabled = false;

    // Capture 2: Icosphere
    engine.appState->core.render.billboardMode = false;
    if (draw_frame(&engine) != GfxResult::Success) {
        cleanup_vulkan_engine(&engine);
        return false;
    }
    bool b2 = verify_and_capture_frame(&engine, "test_icosphere.png");
    bool b_subpass = test_subpass_fusion_standalone(&engine);

    // Capture 3: Billboard Wireframe
    engine.appState->core.render.billboardMode = true;
    engine.appState->core.render.wireframeMode = true;
    if (draw_frame(&engine) != GfxResult::Success) {
        cleanup_vulkan_engine(&engine);
        return false;
    }
    bool b3 = verify_and_capture_frame(&engine, "test_wireframe_billboard.png");

    // Capture 4: Icosphere Wireframe
    engine.appState->core.render.billboardMode = false;
    engine.appState->core.render.wireframeMode = true;
    if (draw_frame(&engine) != GfxResult::Success) {
        cleanup_vulkan_engine(&engine);
        return false;
    }
    bool b4 = verify_and_capture_frame(&engine, "test_wireframe_icosphere.png");

    // Capture 5: Close-up Billboard Singularity (Extreme Proximity)
    // Sphere at (1.25, 1.25, 0.0), Radius 1.0.
    // Camera at distance 1.5 -> 0.5 from surface
    engine.appState->core.camera.position = glm::vec3(1.25f, 1.25f, 1.5f);
    engine.appState->core.camera.yaw = -90.0f;
    engine.appState->core.camera.pitch = 0.0f;
    camera_update_vectors(&engine.appState->core.camera);
    engine.appState->core.render.billboardMode = true;
    engine.appState->core.render.wireframeMode = true;
    if (draw_frame(&engine) != GfxResult::Success) {
        cleanup_vulkan_engine(&engine);
        return false;
    }
    bool b5 = verify_and_capture_frame(&engine, "test_close_billboard.png");

    // Capture 6: Close-up Bloom Dual-Filtering Enabled
    engine.appState->core.render.billboardMode = true;
    engine.appState->core.render.wireframeMode = false;
    engine.appState->core.render.bloomEnabled = true;
    engine.appState->core.render.bloomIntensity = 0.30f;
    if (draw_frame(&engine) != GfxResult::Success) {
        cleanup_vulkan_engine(&engine);
        return false;
    }
    bool b6_close_bloom = verify_and_capture_frame(&engine, "test_close_bloom.png");

    // Capture 7: Close-up Bloom Debug Final Map View (Isolated Bloom)
    engine.appState->core.render.bloomDebugMode = 1;
    if (draw_frame(&engine) != GfxResult::Success) {
        cleanup_vulkan_engine(&engine);
        return false;
    }
    bool b7_close_bloom_debug = verify_and_capture_frame(&engine, "test_close_bloom_debug.png");
    engine.appState->core.render.bloomDebugMode = 0;
    engine.appState->core.render.bloomEnabled = false;

    // Test coverage for default WindowOps wrappers
    const WindowOps* ops = runtime_default_window_ops();
    ops->get_key(engine.appState->window, GLFW_KEY_UNKNOWN);
    ops->set_window_should_close(engine.appState->window, GLFW_FALSE);
    GLFWmonitor* primary = ops->get_primary_monitor();
    if (primary) {
        ops->get_video_mode(primary);
    }
    int winX = 0;
    int winY = 0;
    int winW = 0;
    int winH = 0;
    ops->get_window_pos(engine.appState->window, &winX, &winY);
    ops->get_window_size(engine.appState->window, &winW, &winH);
    ops->set_window_monitor(engine.appState->window, nullptr, winX, winY, winW, winH, 0);

    // Test envmap logic
    vk_adjust_env_lod(&engine, 1.0f);
    vk_adjust_env_lod(&engine, -1.0f);
    vk_switch_environment_texture(&engine, 1);
    vk_switch_environment_texture(&engine, -1);
    engine.iblBaker.ExportMaps(&engine);

    // Cover mouse and scroll callbacks
    engine.appState->core.cameraEnabled = false;
    vk_mouse_callback(engine.appState->window, 10.0, 10.0); // camera off
    engine.appState->core.cameraEnabled = true;
    engine.appState->core.camera.firstMouse = true;
    vk_mouse_callback(engine.appState->window, 10.0, 10.0); // first mouse
    vk_mouse_callback(engine.appState->window, 20.0, 20.0); // move mouse

    vk_scroll_callback(engine.appState->window, 0, 1.0); // scroll

    bool b6 = test_render_graph_vulkan(&engine);
    bool b7 = test_swapchain_resize(&engine);
    bool b_ae = test_autoexposure_synthetic(&engine);

    appState.rhi->Shutdown();
    DestroyRHI(appState.rhi);

    arena_free(&appState.rhiArena);
    arena_free(&appState.core.scene.arena);

    glfwDestroyWindow(appState.window);
    return b1 && b2 && b3 && b4 && b5 && b_bloom && b_bloom_debug && b6_close_bloom && b7_close_bloom_debug && b6 && b7 && b_subpass && b_ae;
}

static bool test_rhi_reload_lifecycle() {
    EngineState appState = {};
    arena_init(&appState.rhiArena, RHI_ARENA_CAPACITY_BYTES);
    core_engine_init(&appState.core);
    appState.core.cameraEnabled = false;

    if (glfwInit() != GLFW_TRUE)
        return false;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    appState.window = glfwCreateWindow(800, 600, "ReloadTest", NULL, NULL);
    if (!appState.window) {
        glfwTerminate();
        arena_free(&appState.rhiArena);
        arena_free(&appState.core.scene.arena);
        return false;
    }

    // Cycle 1: Init -> Draw -> Shutdown -> Destroy
    appState.rhi = CreateRHI(&appState);
    if (!appState.rhi || appState.rhi->Init() != RHIResult::Success || appState.rhi->DrawFrame() != RHIResult::Success) {
        if (appState.rhi) {
            appState.rhi->Shutdown();
            DestroyRHI(appState.rhi);
        }
        glfwDestroyWindow(appState.window);
        glfwTerminate();
        arena_free(&appState.rhiArena);
        arena_free(&appState.core.scene.arena);
        return false;
    }
    appState.rhi->Shutdown();
    DestroyRHI(appState.rhi);
    appState.rhi = nullptr;

    // Cycle 2: Re-Init in same process (Simulate F5 Hot-Reload) -> Draw -> Shutdown -> Destroy
    appState.rhi = CreateRHI(&appState);
    if (!appState.rhi || appState.rhi->Init() != RHIResult::Success || appState.rhi->DrawFrame() != RHIResult::Success) {
        if (appState.rhi) {
            appState.rhi->Shutdown();
            DestroyRHI(appState.rhi);
        }
        glfwDestroyWindow(appState.window);
        glfwTerminate();
        arena_free(&appState.rhiArena);
        arena_free(&appState.core.scene.arena);
        return false;
    }
    appState.rhi->Shutdown();
    DestroyRHI(appState.rhi);
    appState.rhi = nullptr;

    glfwDestroyWindow(appState.window);
    glfwTerminate();
    arena_free(&appState.rhiArena);
    arena_free(&appState.core.scene.arena);
    LOG_INFO("test", "RHI reload lifecycle test PASSED (2 complete cycles without leaks/crashes).");
    return true;
}

extern bool test_render_graph();
extern bool test_render_graph_stress();

int main() {
    if (!test_integration_rendering()) {
        LOG_ERROR("test", "Le test d'integration de rendu a echoue.");
        return 1;
    }

    if (!test_rhi_reload_lifecycle()) {
        LOG_ERROR("test", "Le test de rechargement/cycle de vie RHI a echoue.");
        return 1;
    }

    if (!test_render_graph()) {
        LOG_ERROR("test", "Le test RenderGraph a echoue.");
        return 1;
    }

    if (!test_render_graph_stress()) {
        LOG_ERROR("test", "Le test RenderGraph Stress a echoue.");
        return 1;
    }

    LOG_INFO("test", "TEST PASS : Image generee et validee.");
    return 0;
}
