#ifndef RHI_H
#define RHI_H

#include <cstdint>
#include <vector>

struct Vertex;

using MeshHandle = uint32_t;
using TextureHandle = uint32_t;
using BufferHandle = uint32_t;
using PipelineHandle = uint32_t;

static constexpr uint32_t INVALID_HANDLE = 0xFFFFFFFF;

enum class PipelineType : uint8_t {
    Graphics,
    Billboard,
    Wireframe,
    DebugLine,
    DebugTriangle,
    Skybox
};

enum class SwapchainStatus : uint8_t {
    Ok,
    NeedRecreate,
    Error
};

class IRHI {
public:
    virtual ~IRHI() = default;

    virtual bool Init() = 0;
    virtual void Shutdown() = 0;

    // Frame lifecycle
    virtual SwapchainStatus AcquireNextImage(uint32_t* imageIndex) = 0;
    virtual void UpdateUBO(const struct UBOData& data) = 0;
    virtual bool BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual SwapchainStatus SubmitAndPresent(uint32_t imageIndex) = 0;

    // Render pass
    virtual void BeginRenderPass() = 0;
    virtual void EndRenderPass() = 0;

    // Binding
    virtual void BindPipeline(PipelineType type) = 0;
    virtual void BindGlobalDescriptor() = 0; // Binds the main uniform/material descriptor
    virtual void BindMeshBuffers(bool isBillboard) = 0;

    // Drawing
    virtual void Draw(uint32_t vertexCount, uint32_t instanceCount) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount) = 0;
    virtual void UpdateBillboardInstances(const struct BillboardInstance* instances, size_t count) = 0;
    
    // Debug & Profiling
    virtual void PushDebugConstants(const void* data, uint32_t size) = 0;
    virtual void BeginDebugLabel(const char* name, float r, float g, float b) = 0;
    virtual void EndDebugLabel() = 0;
    virtual void CollectProfiling() = 0;
    virtual void GetResolution(uint32_t* width, uint32_t* height) const = 0;

    // Type-erased accessors for profiling/debug tools
    virtual void* GetOpaqueTracyContext() const = 0;
    virtual void* GetOpaqueCommandBuffer() const = 0;
};

#endif
