#ifndef NULL_RHI_H
#define NULL_RHI_H
#include "rhi.h"

class NullRHI : public IRHI {
  public:
    bool Init() override {
        return true;
    }
    void Shutdown() override {}

    bool DrawFrame() override {
        return true;
    }
    void HandleInputs(const struct WindowOps* ops) override {
        (void)ops;
    }

    BufferHandle CreateBuffer(std::size_t /*size*/, BufferUsage /*usage*/, const void* /*initialData*/ = nullptr, const char* /*name*/ = nullptr) override { return 0; }
    void DestroyBuffer(BufferHandle /*handle*/) override {}
    void* MapBuffer(BufferHandle /*handle*/) override { return nullptr; }
    void UnmapBuffer(BufferHandle /*handle*/) override {}

    TextureHandle CreateTexture(uint32_t /*width*/, uint32_t /*height*/, TextureFormat /*format*/, TextureUsage /*usage*/, uint32_t /*mipLevels*/ = 1, const char* /*name*/ = nullptr) override { return INVALID_HANDLE; }
    void DestroyTexture(TextureHandle /*handle*/) override {}

    ImageViewHandle CreateImageView(TextureHandle /*texture*/, uint32_t /*baseMip*/, uint32_t /*mipCount*/, uint32_t /*baseArray*/, uint32_t /*arrayCount*/) override { return INVALID_HANDLE; }
    void DestroyImageView(ImageViewHandle /*handle*/) override {}

    SamplerHandle CreateSampler(uint32_t /*mipLevels*/ = 1, bool /*clampToEdge*/ = false, const char* /*name*/ = nullptr) override { return INVALID_HANDLE; }
    void DestroySampler(SamplerHandle /*handle*/) override {}

    DescriptorLayoutHandle CreateDescriptorLayout(const DescriptorLayoutDesc& /*desc*/, const char* /*name*/ = nullptr) override { return INVALID_HANDLE; }
    void DestroyDescriptorLayout(DescriptorLayoutHandle /*handle*/) override {}

    DescriptorPoolHandle CreateDescriptorPool(const DescriptorPoolDesc& /*desc*/, const char* /*name*/ = nullptr) override { return INVALID_HANDLE; }
    void DestroyDescriptorPool(DescriptorPoolHandle /*handle*/) override {}

    bool AllocateDescriptorSets(const DescriptorSetAllocateDesc& /*desc*/, DescriptorSetHandle* /*outSets*/) override { return false; }
    void UpdateDescriptorSets(uint32_t /*count*/, const WriteDescriptorSet* /*writes*/) override {}
    PipelineLayoutHandle CreatePipelineLayout(const PipelineLayoutDesc& /*desc*/, const char* /*name*/) override { return INVALID_HANDLE; }
    void DestroyPipelineLayout(PipelineLayoutHandle /*handle*/) override {}

    PipelineHandle CreateComputePipeline(const ComputePipelineDesc& /*desc*/) override { return INVALID_HANDLE; }
    PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& /*desc*/) override { return INVALID_HANDLE; }
    void DestroyPipeline(PipelineHandle /*handle*/) override {}

    SwapchainStatus AcquireNextImage(uint32_t* /*imageIndex*/) override { return SwapchainStatus::Ok; }
    void UpdateUBO(const struct UBOData& /*data*/) override {}
    bool BeginFrame() override {
        return true;
    }
    void EndFrame() override {}
    SwapchainStatus SubmitAndPresent(uint32_t /*imageIndex*/) override { return SwapchainStatus::Ok; }
    void BeginRenderPass() override {}
    void EndRenderPass() override {}
    void CmdBindPipeline(CommandBufferHandle /*cb*/, PipelineHandle /*pipeline*/, bool /*isCompute*/) override {}
    void CmdBindDescriptorSets(CommandBufferHandle /*cb*/, PipelineLayoutHandle /*layout*/, uint32_t /*firstSet*/, uint32_t /*count*/, const DescriptorSetHandle* /*sets*/, bool /*isCompute*/) override {}
    void CmdPushConstants(CommandBufferHandle /*cb*/, PipelineLayoutHandle /*layout*/, ShaderStage /*stage*/, uint32_t /*offset*/, uint32_t /*size*/, const void* /*values*/) override {}
    void CmdDraw(CommandBufferHandle /*cb*/, uint32_t /*vertexCount*/, uint32_t /*instanceCount*/, uint32_t /*firstVertex*/, uint32_t /*firstInstance*/) override {}
    void CmdDrawIndexed(CommandBufferHandle /*cb*/, uint32_t /*indexCount*/, uint32_t /*instanceCount*/, uint32_t /*firstIndex*/, int32_t /*vertexOffset*/, uint32_t /*firstInstance*/) override {}
    void CmdDispatch(CommandBufferHandle /*cb*/, uint32_t /*x*/, uint32_t /*y*/, uint32_t /*z*/) override {}
    void BindPipeline(PipelineType /*type*/) override {}
    void BindGlobalDescriptor() override {}
    void BindMeshBuffers(bool /*isBillboard*/) override {}
    void Draw(uint32_t /*vertexCount*/, uint32_t /*instanceCount*/) override {}
    void DrawIndexed(uint32_t /*indexCount*/, uint32_t /*instanceCount*/) override {}
    void UpdateBillboardInstances(const struct BillboardInstance* /*instances*/, std::size_t /*count*/) override {}
    void PushDebugConstants(const void* /*data*/, uint32_t /*size*/) override {}
    void BeginDebugLabel(const char* /*name*/, float /*r*/, float /*g*/, float /*b*/) override {}
    void EndDebugLabel() override {}
    void CollectProfiling() override {}
    void GetResolution(uint32_t* width, uint32_t* height) const override {
        *width = 800;
        *height = 600;
    }
    void* GetOpaqueTracyContext() const override { return nullptr; }
    void* GetOpaqueCommandBuffer() const override { return nullptr; }
};
#endif
