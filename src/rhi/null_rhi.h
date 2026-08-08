#ifndef NULL_RHI_H
#define NULL_RHI_H
#include "rhi.h"

class NullRHI : public IRHI {
  public:
    bool Init() override {
        return true;
    }
    void Shutdown() override {}

    BufferHandle CreateBuffer(std::size_t /*size*/, BufferUsage /*usage*/, const void* /*initialData*/ = nullptr, const char* /*name*/ = nullptr) override { return 0; }
    void DestroyBuffer(BufferHandle /*handle*/) override {}
    void* MapBuffer(BufferHandle /*handle*/) override { return nullptr; }
    void UnmapBuffer(BufferHandle /*handle*/) override {}

    TextureHandle CreateTexture(uint32_t /*width*/, uint32_t /*height*/, TextureFormat /*format*/, TextureUsage /*usage*/, uint32_t /*mipLevels*/ = 1, const char* /*name*/ = nullptr) override { return INVALID_HANDLE; }
    void DestroyTexture(TextureHandle /*handle*/) override {}

    SamplerHandle CreateSampler(uint32_t /*mipLevels*/ = 1, bool /*clampToEdge*/ = false, const char* /*name*/ = nullptr) override { return INVALID_HANDLE; }
    void DestroySampler(SamplerHandle /*handle*/) override {}

    SwapchainStatus AcquireNextImage(uint32_t* /*imageIndex*/) override { return SwapchainStatus::Ok; }
    void UpdateUBO(const struct UBOData& /*data*/) override {}
    bool BeginFrame() override {
        return true;
    }
    void EndFrame() override {}
    SwapchainStatus SubmitAndPresent(uint32_t /*imageIndex*/) override { return SwapchainStatus::Ok; }
    void BeginRenderPass() override {}
    void EndRenderPass() override {}
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
