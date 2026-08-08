#include "vulkan_rhi.h"
#include "../vk_engine.h"
#include "../tracy_vulkan.h"
#include <cstring>

VulkanRHI::VulkanRHI(VulkanEngine* engine) : _engine(engine) {}

VulkanRHI::~VulkanRHI() {
    Shutdown();
}

bool VulkanRHI::Init() {
    return true;
}

void VulkanRHI::Shutdown() {
}

SwapchainStatus VulkanRHI::AcquireNextImage(uint32_t* imageIndex) {
    if (vkWaitForFences(_engine->device, 1, &_engine->inFlightFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        return SwapchainStatus::Error;
    }
    if (vkResetFences(_engine->device, 1, &_engine->inFlightFence) != VK_SUCCESS) {
        return SwapchainStatus::Error;
    }

    VkResult acquireResult = vkAcquireNextImageKHR(_engine->device, _engine->swapchain, UINT64_MAX, _engine->imageAvailableSemaphore, nullptr, imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        return SwapchainStatus::NeedRecreate;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        return SwapchainStatus::Error;
    }
    return SwapchainStatus::Ok;
}

void VulkanRHI::UpdateUBO(const UBOData& data) {
    memcpy(_engine->uniformBufferMapped, &data, sizeof(UBOData));
}

SwapchainStatus VulkanRHI::SubmitAndPresent(uint32_t imageIndex) {
    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &_engine->imageAvailableSemaphore;
    si.pWaitDstStageMask = &wait;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &_engine->commandBuffer;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &_engine->renderFinishedSemaphore;
    if (vkQueueSubmit(_engine->graphicsQueue, 1, &si, _engine->inFlightFence) != VK_SUCCESS) {
        return SwapchainStatus::Error;
    }
    VkPresentInfoKHR pri{};
    pri.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pri.waitSemaphoreCount = 1;
    pri.pWaitSemaphores = &_engine->renderFinishedSemaphore;
    pri.swapchainCount = 1;
    pri.pSwapchains = &_engine->swapchain;
    pri.pImageIndices = &imageIndex;
    VkResult presentResult = vkQueuePresentKHR(_engine->presentQueue, &pri);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        return SwapchainStatus::NeedRecreate;
    }
    return presentResult == VK_SUCCESS ? SwapchainStatus::Ok : SwapchainStatus::Error;
}

bool VulkanRHI::BeginFrame() {
    if (vkResetCommandBuffer(_engine->commandBuffer, 0) != VK_SUCCESS) {
        return false;
    }
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    return vkBeginCommandBuffer(_engine->commandBuffer, &bi) == VK_SUCCESS;
}

void VulkanRHI::EndFrame() {
    vkEndCommandBuffer(_engine->commandBuffer);
}

void VulkanRHI::BeginRenderPass() {
    VkClearValue cl[2] = {};
    cl[0].color = {{0.05f, 0.05f, 0.2f, 1.0f}};
    cl[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = _engine->renderPass;
    rp.framebuffer = _engine->swapchainFramebuffers[_engine->lastRenderedImageIndex];
    rp.renderArea.extent = _engine->swapchainExtent;
    rp.clearValueCount = 2;
    rp.pClearValues = cl;

    vkCmdBeginRenderPass(_engine->commandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanRHI::EndRenderPass() {
    vkCmdEndRenderPass(_engine->commandBuffer);
}

void VulkanRHI::BindPipeline(PipelineType type) {
    VkPipeline pipeline = VK_NULL_HANDLE;
    switch (type) {
    case PipelineType::Graphics:
        pipeline = _engine->graphicsPipeline;
        break;
    case PipelineType::Billboard:
        pipeline = _engine->billboardPipeline;
        break;
    case PipelineType::Wireframe:
        pipeline = _engine->wireframePipeline;
        break;
    case PipelineType::DebugLine:
        pipeline = _engine->debugLinePipeline;
        break;
    case PipelineType::DebugTriangle:
        pipeline = _engine->debugTrianglePipeline;
        break;
    case PipelineType::Skybox:
        pipeline = _engine->skyboxPipeline;
        break;
    }
    if (pipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(_engine->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    }
}

void VulkanRHI::BindGlobalDescriptor() {
    vkCmdBindDescriptorSets(_engine->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _engine->pipelineLayout, 0, 1, &_engine->descriptorSet, 0, nullptr);
}

void VulkanRHI::BindMeshBuffers(bool isBillboard) {
    if (isBillboard) {
        VkBuffer buffers[] = {_engine->billboardBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(_engine->commandBuffer, 1, 1, buffers, offsets);
    } else {
        VkBuffer buffers[] = {_engine->vertexBuffer, _engine->instanceBuffer};
        VkDeviceSize offsets[] = {0, 0};
        vkCmdBindVertexBuffers(_engine->commandBuffer, 0, 2, buffers, offsets);
        vkCmdBindIndexBuffer(_engine->commandBuffer, _engine->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    }
}

void VulkanRHI::Draw(uint32_t vertexCount, uint32_t instanceCount) {
    vkCmdDraw(_engine->commandBuffer, vertexCount, instanceCount, 0, 0);
}

void VulkanRHI::DrawIndexed(uint32_t indexCount, uint32_t instanceCount) {
    vkCmdDrawIndexed(_engine->commandBuffer, indexCount, instanceCount, 0, 0, 0);
}

void VulkanRHI::PushDebugConstants(const void* data, uint32_t size) {
    vkCmdPushConstants(_engine->commandBuffer, _engine->debugPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, size, data);
}

void VulkanRHI::UpdateBillboardInstances(const BillboardInstance* instances, size_t count) {
    if (_engine->billboardMapped) {
        memcpy(_engine->billboardMapped, instances, count * sizeof(BillboardInstance));
    }
}

void VulkanRHI::BeginDebugLabel(const char* name, float r, float g, float b) {
    vk_begin_label(_engine->device, _engine->commandBuffer, name, r, g, b);
}

void VulkanRHI::EndDebugLabel() {
    vk_end_label(_engine->device, _engine->commandBuffer);
}

void VulkanRHI::CollectProfiling() {
    tracy_vk_collect(_engine, _engine->commandBuffer);
}

void VulkanRHI::GetResolution(uint32_t* width, uint32_t* height) const {
    *width = _engine->swapchainExtent.width;
    *height = _engine->swapchainExtent.height;
}

void* VulkanRHI::GetOpaqueTracyContext() const {
    return _engine->tracyVkContext;
}

void* VulkanRHI::GetOpaqueCommandBuffer() const {
    return _engine->commandBuffer;
}

