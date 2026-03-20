#include "vk_engine_frame.h"

#include "vk_engine_envmap.h"
#include "vk_engine_runtime.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace {

constexpr uint32_t kGridSize = 10;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 1000.0f;
constexpr float kLegacyDefaultFov = 60.0f;

} // namespace

void vk_update_animation_clock(VulkanEngine* engine, float maxFrameDeltaSeconds) {
    const auto now = std::chrono::steady_clock::now();

    if (engine->lastFrameTimestamp.time_since_epoch().count() == 0) {
        engine->lastFrameTimestamp = now;
        return;
    }

    float deltaSeconds = std::chrono::duration<float>(now - engine->lastFrameTimestamp).count();
    engine->lastFrameTimestamp = now;
    deltaSeconds = std::clamp(deltaSeconds, 0.0f, maxFrameDeltaSeconds);
    engine->lastFrameDeltaSeconds = deltaSeconds;

    if (!engine->animationPaused) {
        engine->animationTimeSeconds += deltaSeconds * engine->animationSpeed;
    }
}

bool vk_draw_frame_internal(VulkanEngine* engine, RecreateSwapchainFn recreateSwapchain) {
    if (vkWaitForFences(engine->device, 1, &engine->inFlightFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        return false;
    }
    if (vkResetFences(engine->device, 1, &engine->inFlightFence) != VK_SUCCESS) {
        return false;
    }

    vk_update_animation_clock(engine, 0.25f);
    vk_process_ready_environment_texture(engine);
    vk_handle_runtime_input(engine);
    vk_update_camera_key_state(engine);
    camera_fixed_update(&engine->camera, engine->lastFrameDeltaSeconds);

    uint32_t idx;
    const VkResult acquireResult = vkAcquireNextImageKHR(engine->device, engine->swapchain, UINT64_MAX, engine->imageAvailableSemaphore, nullptr, &idx);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        return recreateSwapchain(engine);
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        return false;
    }
    engine->lastRenderedImageIndex = idx;

    UBOData uboData;
    uboData.modelRotation = glm::mat4(1.0f);
    glm::mat4 view = glm::lookAt(engine->camera.position, engine->camera.position + engine->camera.front, engine->camera.up);
    glm::mat4 proj =
        glm::perspective(glm::radians(engine->camera.zoom),
                         static_cast<float>(engine->swapchainExtent.width) / static_cast<float>(engine->swapchainExtent.height), kNearPlane, kFarPlane);
    proj[1][1] *= -1;
    uboData.vp = proj * view;

    glm::mat4 skyboxView = glm::mat4(glm::mat3(view));
    glm::mat4 skyboxProj =
        glm::perspective(glm::radians(kLegacyDefaultFov),
                         static_cast<float>(engine->swapchainExtent.width) / static_cast<float>(engine->swapchainExtent.height), kNearPlane, kFarPlane);
    skyboxProj[1][1] *= -1;
    uboData.invViewProj = glm::inverse(skyboxProj * skyboxView);

    uboData.cameraPosEnvLod = glm::vec4(engine->camera.position, engine->envLod);
    uboData.debugParams = glm::vec4(static_cast<float>(engine->iblDebugMode), engine->iblDebugScale, engine->billboardMode ? 1.0f : 0.0f, 0.0f);
    uboData.postParams1 = glm::vec4(engine->exposure, engine->saturation, engine->contrast, engine->gamma);
    uboData.postParams2 = glm::vec4(engine->gain, engine->offset, engine->wbTemp, engine->wbTint);
    uboData.view = view;
    uboData.proj = proj;

    int width;
    int height;
    glfwGetFramebufferSize(engine->window, &width, &height);
    uboData.windowSize = glm::vec4(static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f);

    memcpy(engine->uniformBufferMapped, &uboData, sizeof(uboData));

    if (vkResetCommandBuffer(engine->commandBuffer, 0) != VK_SUCCESS) {
        return false;
    }
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(engine->commandBuffer, &bi) != VK_SUCCESS) {
        return false;
    }

    vk_begin_label(engine->device, engine->commandBuffer, "Render_Frame_Graphics", 1.0f, 0.5f, 0.0f);

    VkClearValue cl[2] = {};
    cl[0].color = {{0.05f, 0.05f, 0.2f, 1.0f}};
    cl[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = engine->renderPass;
    rp.framebuffer = engine->swapchainFramebuffers[idx];
    rp.renderArea.extent = engine->swapchainExtent;
    rp.clearValueCount = 2;
    rp.pClearValues = cl;

    vk_begin_label(engine->device, engine->commandBuffer, "RenderPass_Begin_And_Bindings", 1.0f, 0.8f, 0.2f);
    vkCmdBeginRenderPass(engine->commandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindDescriptorSets(engine->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, engine->pipelineLayout, 0, 1, &engine->descriptorSet, 0, nullptr);
    vk_end_label(engine->device, engine->commandBuffer);

    vk_begin_label(engine->device, engine->commandBuffer, "Render_Skybox_EnvMap", 0.2f, 0.5f, 1.0f);
    if (engine->showEnvmap) {
        vkCmdBindPipeline(engine->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, engine->skyboxPipeline);
        vkCmdDraw(engine->commandBuffer, 3, 1, 0, 0);
    }
    vk_end_label(engine->device, engine->commandBuffer);

    vk_begin_label(engine->device, engine->commandBuffer, "Render_Spheres_Instanced", 0.0f, 1.0f, 0.4f);
    if (engine->billboardMode) {
        // --- Sort Billboards Back-to-Front (Painter's Algorithm) ---
        const glm::vec3 camPos = engine->camera.position;
        std::sort(engine->billboardInstances.begin(), engine->billboardInstances.end(), [&camPos](const BillboardInstance& a, const BillboardInstance& b) {
            glm::vec3 da = a.pos - camPos;
            glm::vec3 db = b.pos - camPos;
            return glm::dot(da, da) > glm::dot(db, db); // Back-to-front
        });

        // --- Update Persistently Mapped Billboard Buffer ---
        if (engine->billboardMapped) {
            memcpy(engine->billboardMapped, engine->billboardInstances.data(), engine->billboardInstances.size() * sizeof(BillboardInstance));
        }

        vkCmdBindPipeline(engine->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, engine->billboardPipeline);
        VkBuffer billboardBuffers[] = {engine->billboardBuffer};
        VkDeviceSize billboardOffsets[] = {0};
        vkCmdBindVertexBuffers(engine->commandBuffer, 1, 1, billboardBuffers, billboardOffsets);
        vkCmdDraw(engine->commandBuffer, 6, static_cast<uint32_t>(engine->billboardInstances.size()), 0, 0);
    } else {
        VkPipeline pipe = engine->wireframeMode ? engine->wireframePipeline : engine->graphicsPipeline;
        vkCmdBindPipeline(engine->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        VkBuffer vertexBuffers[] = {engine->vertexBuffer, engine->instanceBuffer};
        VkDeviceSize offsets[] = {0, 0};
        vkCmdBindVertexBuffers(engine->commandBuffer, 0, 2, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(engine->commandBuffer, engine->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(engine->commandBuffer, engine->indexCount, kGridSize * kGridSize, 0, 0, 0);
    }

    if (engine->wireframeMode && engine->billboardMode) {
        // Billboard Wireframe Overlays (Legacy parity)
        DebugPushConstant dp = {};
        dp.model = glm::mat4(1.0f);
        dp.radius = 1.0f; // Standard radius for spheres in this app

        // 1. Transparent Fill
        vkCmdBindPipeline(engine->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, engine->debugTrianglePipeline);
        dp.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.1f);
        dp.mode = 1;
        dp.stippled = 2; // Triangle mode flag
        vkCmdPushConstants(engine->commandBuffer, engine->debugPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(DebugPushConstant), &dp);
        vkCmdDraw(engine->commandBuffer, 6, kGridSize * kGridSize, 0, 0);

        // 2. Green Quad Outlines
        vkCmdBindPipeline(engine->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, engine->debugLinePipeline);
        dp.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
        dp.mode = 1;
        dp.stippled = 0;
        vkCmdPushConstants(engine->commandBuffer, engine->debugPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(DebugPushConstant), &dp);
        vkCmdDraw(engine->commandBuffer, 8, kGridSize * kGridSize, 0, 0);

        // 3. Yellow Stippled Boxes
        dp.color = glm::vec4(1.0f, 1.0f, 0.0f, 0.5f);
        dp.mode = 0;
        dp.stippled = 1;
        vkCmdPushConstants(engine->commandBuffer, engine->debugPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(DebugPushConstant), &dp);
        vkCmdDraw(engine->commandBuffer, 24, kGridSize * kGridSize, 0, 0);
    }
    vk_end_label(engine->device, engine->commandBuffer);

    vkCmdEndRenderPass(engine->commandBuffer);
    vk_end_label(engine->device, engine->commandBuffer);
    if (vkEndCommandBuffer(engine->commandBuffer) != VK_SUCCESS) {
        return false;
    }

    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &engine->imageAvailableSemaphore;
    si.pWaitDstStageMask = &wait;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &engine->commandBuffer;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &engine->renderFinishedSemaphore;

    if (vkQueueSubmit(engine->graphicsQueue, 1, &si, engine->inFlightFence) != VK_SUCCESS) {
        return false;
    }

    VkPresentInfoKHR pri{};
    pri.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pri.waitSemaphoreCount = 1;
    pri.pWaitSemaphores = &engine->renderFinishedSemaphore;
    pri.swapchainCount = 1;
    pri.pSwapchains = &engine->swapchain;
    pri.pImageIndices = &idx;
    const VkResult presentResult = vkQueuePresentKHR(engine->presentQueue, &pri);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        return recreateSwapchain(engine);
    }
    return presentResult == VK_SUCCESS;
}
