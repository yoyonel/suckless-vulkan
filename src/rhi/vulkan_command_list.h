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
    void BeginRenderPassLoad() override;
    void EndRenderPass() override;

    void BindPipeline(PipelineHandle pipeline, bool isCompute = false) override;
    void BindDescriptorSets(PipelineLayoutHandle layout, uint32_t firstSet, uint32_t count, const DescriptorSetHandle* sets, bool isCompute = false) override;
    void BindDescriptorSetsWithDynamicOffsets(PipelineLayoutHandle layout, uint32_t firstSet, uint32_t count, const DescriptorSetHandle* sets, uint32_t dynamicOffsetCount, const uint32_t* dynamicOffsets, bool isCompute = false) override;
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

    void TransitionTexture(TextureHandle texture, ResourceState newState) override;
    void TransitionBuffer(BufferHandle buffer, ResourceState newState) override;
    void FlushBarriers() override;

    void BindComputePipeline(PipelineHandle pipeline) override;
    void SetBindGroup(uint32_t setIndex, BindGroupHandle bindGroup, PipelineLayoutHandle pipelineLayout = INVALID_HANDLE) override;
    void PushComputeConstants(const void* data, uint32_t size, uint32_t offset = 0) override;
    void DispatchCompute(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;

    void BindGraphicsPipeline(PipelineHandle pipeline) override;
    void SetGraphicsBindGroup(uint32_t setIndex, BindGroupHandle bindGroup, uint32_t dynamicOffsetCount = 0, const uint32_t* dynamicOffsets = nullptr) override;
    void PushGraphicsConstants(ShaderStage stage, uint32_t offset, uint32_t size, const void* values) override;

    // Helper temporaire pour les barrières IBL
    VkCommandBuffer GetVkCommandBuffer() const { return m_cmdBuffer; }

private:
    VulkanRHI* m_rhi;
    VkCommandBuffer m_cmdBuffer;

    VkPipelineLayout m_currentComputePipelineLayout{VK_NULL_HANDLE};
    VkPipelineLayout m_currentGraphicsPipelineLayout{VK_NULL_HANDLE};

    std::vector<VkImageMemoryBarrier> m_pendingImageBarriers;
    std::vector<VkBufferMemoryBarrier> m_pendingBufferBarriers;
    VkPipelineStageFlags m_srcStageMask{0};
    VkPipelineStageFlags m_dstStageMask{0};
};

#endif // RHI_VULKAN_COMMAND_LIST_H
