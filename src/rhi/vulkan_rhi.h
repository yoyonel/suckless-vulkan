#ifndef VULKAN_RHI_H
#define VULKAN_RHI_H

#include "rhi.h"
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

class VulkanRHI : public IRHI {
public:
    VulkanRHI(struct VulkanEngine* engine);
    ~VulkanRHI() override;

    bool Init() override;
    void Shutdown() override;

    bool BeginFrame() override;
    void EndFrame() override;

    SwapchainStatus AcquireNextImage(uint32_t* imageIndex) override;
    void UpdateUBO(const struct UBOData& data) override;
    SwapchainStatus SubmitAndPresent(uint32_t imageIndex) override;

    void BeginRenderPass() override;
    void EndRenderPass() override;

    void BindPipeline(PipelineType type) override;
    void BindGlobalDescriptor() override;
    void BindMeshBuffers(bool isBillboard) override;

    void Draw(uint32_t vertexCount, uint32_t instanceCount) override;
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount) override;
    void UpdateBillboardInstances(const struct BillboardInstance* instances, size_t count) override;
    
    void PushDebugConstants(const void* data, uint32_t size) override;
    void BeginDebugLabel(const char* name, float r, float g, float b) override;
    void EndDebugLabel() override;
    void CollectProfiling() override;
    void GetResolution(uint32_t* width, uint32_t* height) const override;
    void* GetOpaqueTracyContext() const override;
    void* GetOpaqueCommandBuffer() const override;

private:
    struct VulkanEngine* _engine;
};

#endif
