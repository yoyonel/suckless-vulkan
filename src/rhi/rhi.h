#ifndef RHI_H
#define RHI_H

#include <cstdint>
#include <cstddef>
#include <vector>

struct Vertex;

using MeshHandle = uint32_t;
using TextureHandle = uint32_t;
using BufferHandle = uint32_t;
using PipelineHandle = uint32_t;
using SamplerHandle = uint32_t;

static constexpr uint32_t INVALID_HANDLE = 0xFFFFFFFF;

enum class TextureFormat : uint8_t {
    RGBA8_UNORM,
    RGBA32_SFLOAT,
    RGBA16_SFLOAT,
    RG16_SFLOAT,
    Depth,
};

enum class TextureUsage : uint8_t {
    Sampled,
    DepthAttachment,
    ColorAttachment,
    Storage
};

enum class BufferUsage : uint8_t {
    Vertex,
    Index,
    Uniform,
    Storage
};


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

    // Resources
    virtual BufferHandle CreateBuffer(std::size_t size, BufferUsage usage, const void* initialData = nullptr, const char* name = nullptr) = 0;
    virtual void DestroyBuffer(BufferHandle handle) = 0;
    virtual void* MapBuffer(BufferHandle handle) = 0;
    virtual void UnmapBuffer(BufferHandle handle) = 0;

    virtual TextureHandle CreateTexture(uint32_t width, uint32_t height, TextureFormat format, TextureUsage usage, uint32_t mipLevels = 1, const char* name = nullptr) = 0;
    virtual void DestroyTexture(TextureHandle handle) = 0;

    virtual SamplerHandle CreateSampler(uint32_t mipLevels = 1, bool clampToEdge = false, const char* name = nullptr) = 0;
    virtual void DestroySampler(SamplerHandle handle) = 0;

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
    virtual void UpdateBillboardInstances(const struct BillboardInstance* instances, std::size_t count) = 0;
    
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
