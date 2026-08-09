#include <vector>
#include "vulkan_command_list.h"
#include "vulkan_rhi.h"

VulkanCommandList::VulkanCommandList(VulkanRHI* rhi, VkCommandBuffer cmdBuffer)
    : m_rhi(rhi), m_cmdBuffer(cmdBuffer) {
}

void VulkanCommandList::BeginRenderPass() {
    m_rhi->BeginRenderPass();
}

void VulkanCommandList::EndRenderPass() {
    m_rhi->EndRenderPass();
}

void VulkanCommandList::BindPipeline(PipelineHandle pipeline, bool isCompute) {
    vkCmdBindPipeline(m_cmdBuffer, isCompute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS, m_rhi->GetVkPipeline(pipeline));
}

void VulkanCommandList::BindDescriptorSets(PipelineLayoutHandle layout, uint32_t firstSet, uint32_t count, const DescriptorSetHandle* sets, bool isCompute) {
    std::vector<VkDescriptorSet> vkSets(count);
    for (uint32_t i = 0; i < count; ++i) {
        vkSets[i] = m_rhi->GetVkDescriptorSet(sets[i]);
    }
    vkCmdBindDescriptorSets(m_cmdBuffer, isCompute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS, m_rhi->GetVkPipelineLayout(layout), firstSet, count, vkSets.data(), 0, nullptr);
}

void VulkanCommandList::PushConstants(PipelineLayoutHandle layout, ShaderStage stage, uint32_t offset, uint32_t size, const void* values) {
    VkShaderStageFlags vkStage = 0;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(ShaderStage::Vertex)) vkStage |= VK_SHADER_STAGE_VERTEX_BIT;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(ShaderStage::Fragment)) vkStage |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(ShaderStage::Compute)) vkStage |= VK_SHADER_STAGE_COMPUTE_BIT;
    vkCmdPushConstants(m_cmdBuffer, m_rhi->GetVkPipelineLayout(layout), vkStage, offset, size, values);
}

void VulkanCommandList::BindVertexBuffers(uint32_t firstBinding, uint32_t bindingCount, const BufferHandle* buffers, const uint64_t* offsets) {
    std::vector<VkBuffer> vkBuffers(bindingCount);
    for (uint32_t i = 0; i < bindingCount; ++i) {
        vkBuffers[i] = m_rhi->GetVkBuffer(buffers[i]);
    }
    vkCmdBindVertexBuffers(m_cmdBuffer, firstBinding, bindingCount, vkBuffers.data(), offsets);
}

void VulkanCommandList::BindIndexBuffer(BufferHandle buffer, uint64_t offset, uint32_t indexType) {
    vkCmdBindIndexBuffer(m_cmdBuffer, m_rhi->GetVkBuffer(buffer), offset, (VkIndexType)indexType);
}

void VulkanCommandList::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    vkCmdDraw(m_cmdBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    vkCmdDrawIndexed(m_cmdBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandList::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
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

void VulkanCommandList::BeginDebugLabel(const char* /*name*/, float /*r*/, float /*g*/, float /*b*/) {}
void VulkanCommandList::EndDebugLabel() {}
void VulkanCommandList::InsertDebugLabel(const char* /*name*/, float /*r*/, float /*g*/, float /*b*/) {}
