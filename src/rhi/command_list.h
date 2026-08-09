#ifndef RHI_COMMAND_LIST_H
#define RHI_COMMAND_LIST_H

#include "rhi/rhi.h"

class IRenderCommandList {
public:
    virtual ~IRenderCommandList() = default;

    virtual void BeginRenderPass() = 0;
    virtual void EndRenderPass() = 0;

    virtual void BindPipeline(PipelineHandle pipeline, bool isCompute = false) = 0;
    virtual void BindDescriptorSets(PipelineLayoutHandle layout, uint32_t firstSet, uint32_t count, const DescriptorSetHandle* sets, bool isCompute = false) = 0;
    virtual void PushConstants(PipelineLayoutHandle layout, ShaderStage stage, uint32_t offset, uint32_t size, const void* values) = 0;

    virtual void BindVertexBuffers(uint32_t firstBinding, uint32_t bindingCount, const BufferHandle* buffers, const uint64_t* offsets) = 0;
    virtual void BindIndexBuffer(BufferHandle buffer, uint64_t offset, uint32_t indexType) = 0;

    virtual void Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) = 0;
    virtual void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;

    virtual void SetViewport(float x, float y, float width, float height, float minDepth, float maxDepth) = 0;
    virtual void SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) = 0;

    virtual void BeginDebugLabel(const char* name, float r, float g, float b) = 0;
    virtual void EndDebugLabel() = 0;
    virtual void InsertDebugLabel(const char* name, float r, float g, float b) = 0;
};

#endif // RHI_COMMAND_LIST_H
