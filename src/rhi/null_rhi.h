#ifndef NULL_RHI_H
#define NULL_RHI_H
#include "rhi.h"

class NullRHI : public IRHI {
  public:
    bool Init() override {
        return true;
    }
    void Shutdown() override {}
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
    void UpdateBillboardInstances(const struct BillboardInstance* /*instances*/, size_t /*count*/) override {}
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
