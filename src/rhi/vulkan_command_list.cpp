#include <vector>
#include "vulkan_command_list.h"
#include "vulkan_rhi.h"
#include "vulkan_state_mapper.h"
#include "../vk_engine.h"

VulkanCommandList::VulkanCommandList(VulkanRHI* rhi, VkCommandBuffer cmdBuffer)
    : m_rhi(rhi), m_cmdBuffer(cmdBuffer) {
    m_pendingImageBarriers.reserve(16);
    m_pendingBufferBarriers.reserve(16);
}

void VulkanCommandList::BeginRenderPass() {
    FlushBarriers();
    m_rhi->BeginRenderPass();
}

void VulkanCommandList::BeginRenderPassLoad() {
    FlushBarriers();
    m_rhi->BeginRenderPassLoad();
}

void VulkanCommandList::EndRenderPass() {
    m_rhi->EndRenderPass();
}

void VulkanCommandList::BindPipeline(PipelineHandle pipeline, bool isCompute) {
    if (isCompute) {
        m_currentComputePipelineLayout = m_rhi->GetPipelineLayoutForPipeline(pipeline);
    } else {
        m_currentGraphicsPipelineLayout = m_rhi->GetPipelineLayoutForPipeline(pipeline);
    }
    vkCmdBindPipeline(m_cmdBuffer, isCompute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS, m_rhi->GetVkPipeline(pipeline));
}

void VulkanCommandList::BindComputePipeline(PipelineHandle pipeline) {
    BindPipeline(pipeline, true);
}

void VulkanCommandList::SetBindGroup(uint32_t setIndex, BindGroupHandle bindGroup, PipelineLayoutHandle pipelineLayout) {
    VkDescriptorSet ds = m_rhi->GetVkBindGroup(bindGroup);
    VkPipelineLayout layout = pipelineLayout != INVALID_HANDLE ? m_rhi->GetVkPipelineLayout(pipelineLayout) : m_currentComputePipelineLayout;
    if (ds != VK_NULL_HANDLE && layout != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(m_cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, setIndex, 1, &ds, 0, nullptr);
    }
}

void VulkanCommandList::PushComputeConstants(const void* data, uint32_t size, uint32_t offset) {
    if (m_currentComputePipelineLayout != VK_NULL_HANDLE && data && size > 0) {
        vkCmdPushConstants(m_cmdBuffer, m_currentComputePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, offset, size, data);
    }
}

void VulkanCommandList::DispatchCompute(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    Dispatch(groupCountX, groupCountY, groupCountZ);
}

void VulkanCommandList::BindGraphicsPipeline(PipelineHandle pipeline) {
    BindPipeline(pipeline, false);
}

void VulkanCommandList::SetGraphicsBindGroup(uint32_t setIndex, BindGroupHandle bindGroup, uint32_t dynamicOffsetCount, const uint32_t* dynamicOffsets) {
    VkDescriptorSet ds = m_rhi->GetVkBindGroup(bindGroup);
    if (ds != VK_NULL_HANDLE && m_currentGraphicsPipelineLayout != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(m_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_currentGraphicsPipelineLayout, setIndex, 1, &ds, dynamicOffsetCount, dynamicOffsets);
    }
}

void VulkanCommandList::PushGraphicsConstants(ShaderStage stage, uint32_t offset, uint32_t size, const void* values) {
    if (m_currentGraphicsPipelineLayout != VK_NULL_HANDLE && values && size > 0) {
        VkShaderStageFlags vkStage = 0;
        if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(ShaderStage::Vertex)) vkStage |= VK_SHADER_STAGE_VERTEX_BIT;
        if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(ShaderStage::Fragment)) vkStage |= VK_SHADER_STAGE_FRAGMENT_BIT;
        vkCmdPushConstants(m_cmdBuffer, m_currentGraphicsPipelineLayout, vkStage, offset, size, values);
    }
}

void VulkanCommandList::BindDescriptorSets(PipelineLayoutHandle layout, uint32_t firstSet, uint32_t count, const DescriptorSetHandle* sets, bool isCompute) {
    VkDescriptorSet* vkSets = static_cast<VkDescriptorSet*>(__builtin_alloca(count * sizeof(VkDescriptorSet)));
    for (uint32_t i = 0; i < count; ++i) {
        vkSets[i] = m_rhi->GetVkDescriptorSet(sets[i]);
    }
    vkCmdBindDescriptorSets(m_cmdBuffer, isCompute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS, m_rhi->GetVkPipelineLayout(layout), firstSet, count, vkSets, 0, nullptr);
}

void VulkanCommandList::BindDescriptorSetsWithDynamicOffsets(PipelineLayoutHandle layout, uint32_t firstSet, uint32_t count, const DescriptorSetHandle* sets, uint32_t dynamicOffsetCount, const uint32_t* dynamicOffsets, bool isCompute) {
    VkDescriptorSet* vkSets = static_cast<VkDescriptorSet*>(__builtin_alloca(count * sizeof(VkDescriptorSet)));
    for (uint32_t i = 0; i < count; ++i) {
        vkSets[i] = m_rhi->GetVkDescriptorSet(sets[i]);
    }
    vkCmdBindDescriptorSets(m_cmdBuffer, isCompute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS, m_rhi->GetVkPipelineLayout(layout), firstSet, count, vkSets, dynamicOffsetCount, dynamicOffsets);
}

void VulkanCommandList::PushConstants(PipelineLayoutHandle layout, ShaderStage stage, uint32_t offset, uint32_t size, const void* values) {
    VkShaderStageFlags vkStage = 0;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(ShaderStage::Vertex)) vkStage |= VK_SHADER_STAGE_VERTEX_BIT;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(ShaderStage::Fragment)) vkStage |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(ShaderStage::Compute)) vkStage |= VK_SHADER_STAGE_COMPUTE_BIT;
    vkCmdPushConstants(m_cmdBuffer, m_rhi->GetVkPipelineLayout(layout), vkStage, offset, size, values);
}

void VulkanCommandList::BindVertexBuffers(uint32_t firstBinding, uint32_t bindingCount, const BufferHandle* buffers, const uint64_t* offsets) {
    VkBuffer* vkBuffers = static_cast<VkBuffer*>(__builtin_alloca(bindingCount * sizeof(VkBuffer)));
    for (uint32_t i = 0; i < bindingCount; ++i) {
        vkBuffers[i] = m_rhi->GetVkBuffer(buffers[i]);
    }
    vkCmdBindVertexBuffers(m_cmdBuffer, firstBinding, bindingCount, vkBuffers, offsets);
}

void VulkanCommandList::BindIndexBuffer(BufferHandle buffer, uint64_t offset, uint32_t indexType) {
    vkCmdBindIndexBuffer(m_cmdBuffer, m_rhi->GetVkBuffer(buffer), offset, (VkIndexType)indexType);
}

void VulkanCommandList::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    FlushBarriers();
    vkCmdDraw(m_cmdBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    FlushBarriers();
    vkCmdDrawIndexed(m_cmdBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandList::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    FlushBarriers();
    vkCmdDispatch(m_cmdBuffer, groupCountX, groupCountY, groupCountZ);
}

void VulkanCommandList::SetViewport(float x, float y, float width, float height, float minDepth, float maxDepth) {
    VkViewport vp{x, y, width, height, minDepth, maxDepth};
    vkCmdSetViewport(m_cmdBuffer, 0, 1, &vp);
}

void VulkanCommandList::SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    VkRect2D sc{};
    sc.offset = {x, y};
    sc.extent = {width, height};
    vkCmdSetScissor(m_cmdBuffer, 0, 1, &sc);
}

void VulkanCommandList::BeginDebugLabel(const char* name, float r, float g, float b) {
    vk_begin_label(m_rhi->_engine->ctx.device, m_cmdBuffer, name, r, g, b);
}
void VulkanCommandList::EndDebugLabel() {
    vk_end_label(m_rhi->_engine->ctx.device, m_cmdBuffer);
}
void VulkanCommandList::InsertDebugLabel(const char* /*name*/, float /*r*/, float /*g*/, float /*b*/) {}

void VulkanCommandList::TransitionTexture(TextureHandle texture, ResourceState newState) {
    if (texture == INVALID_HANDLE || !m_rhi) {
        return;
    }
    ResourceState oldState = m_rhi->GetTextureState(texture);
    if (oldState == newState) {
        return;
    }
    VkImage image = m_rhi->GetVkImage(texture);
    if (image == VK_NULL_HANDLE) {
        return;
    }

    VulkanStateMapping oldMapping = map_resource_state_to_vulkan(oldState);
    VulkanStateMapping newMapping = map_resource_state_to_vulkan(newState);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldMapping.layout;
    barrier.newLayout = newMapping.layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = m_rhi->IsTextureDepth(texture) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = m_rhi->GetTextureMipLevels(texture);
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = oldMapping.accessMask;
    barrier.dstAccessMask = newMapping.accessMask;

    m_srcStageMask |= oldMapping.stageMask;
    m_dstStageMask |= newMapping.stageMask;
    m_rhi->SetTextureState(texture, newState);

    m_pendingImageBarriers.push_back(barrier);
}

void VulkanCommandList::TransitionBuffer(BufferHandle buffer, ResourceState newState) {
    if (buffer == INVALID_HANDLE || !m_rhi) {
        return;
    }
    ResourceState oldState = m_rhi->GetBufferState(buffer);
    if (oldState == newState) {
        return;
    }
    VkBuffer vkBuffer = m_rhi->GetVkBuffer(buffer);
    if (vkBuffer == VK_NULL_HANDLE) {
        return;
    }

    VulkanStateMapping oldMapping = map_resource_state_to_vulkan(oldState);
    VulkanStateMapping newMapping = map_resource_state_to_vulkan(newState);

    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = vkBuffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    barrier.srcAccessMask = oldMapping.accessMask;
    barrier.dstAccessMask = newMapping.accessMask;

    m_srcStageMask |= oldMapping.stageMask;
    m_dstStageMask |= newMapping.stageMask;
    m_rhi->SetBufferState(buffer, newState);

    m_pendingBufferBarriers.push_back(barrier);
}

void VulkanCommandList::FlushBarriers() {
    if (m_pendingImageBarriers.empty() && m_pendingBufferBarriers.empty()) {
        return;
    }

    VkPipelineStageFlags srcStage = m_srcStageMask != 0 ? m_srcStageMask : static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    VkPipelineStageFlags dstStage = m_dstStageMask != 0 ? m_dstStageMask : static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    m_rhi->CmdPipelineBarrier(
        m_cmdBuffer,
        srcStage,
        dstStage,
        0,
        0, nullptr,
        static_cast<uint32_t>(m_pendingBufferBarriers.size()), m_pendingBufferBarriers.data(),
        static_cast<uint32_t>(m_pendingImageBarriers.size()), m_pendingImageBarriers.data()
    );

    m_pendingImageBarriers.clear();
    m_pendingBufferBarriers.clear();
    m_srcStageMask = 0;
    m_dstStageMask = 0;
}
