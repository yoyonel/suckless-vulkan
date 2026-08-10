#ifndef RHI_VULKAN_COMMAND_LIST_H
#define RHI_VULKAN_COMMAND_LIST_H

#include "command_list.h"
#include <vulkan/vulkan.h>

class VulkanRHI;

class VulkanCommandList : public IRenderCommandList {
public:
    VulkanCommandList(VulkanRHI* rhi, VkCommandBuffer cmdBuffer);
    ~VulkanCommandList() override = default;

    void BeginRenderPass() override;
    void EndRenderPass() override;

    void BindPipeline(PipelineHandle pipeline, bool isCompute = false) override;
    void BindDescriptorSets(PipelineLayoutHandle layout, uint32_t firstSet, uint32_t count, const DescriptorSetHandle* sets, bool isCompute = false) override;
    void PushConstants(PipelineLayoutHandle layout, ShaderStage stage, uint32_t offset, uint32_t size, const void* values) override;

    void BindVertexBuffers(uint32_t firstBinding, uint32_t bindingCount, const BufferHandle* buffers, const uint64_t* offsets) override;
    void BindIndexBuffer(BufferHandle buffer, uint64_t offset, uint32_t indexType) override;

    void Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) override;
    void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;

    void SetViewport(float x, float y, float width, float height, float minDepth, float maxDepth) override;
    void SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) override;

    void BeginDebugLabel(const char* name, float r, float g, float b) override;
    void EndDebugLabel() override;
    void InsertDebugLabel(const char* name, float r, float g, float b) override;

    // Helper temporaire pour les barrières IBL
    VkCommandBuffer GetVkCommandBuffer() const { return m_cmdBuffer; }

private:
    VulkanRHI* m_rhi;
    VkCommandBuffer m_cmdBuffer;
};

#endif // RHI_VULKAN_COMMAND_LIST_H
