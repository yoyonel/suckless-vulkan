#ifndef RHI_H
#define RHI_H

#include <cstdint>
#include <cstddef>
#include <vector>

struct Vertex;

using MeshHandle = uint32_t;
using TextureHandle = uint32_t;
using ImageViewHandle = uint32_t;
using BufferHandle = uint32_t;
using PipelineHandle = uint32_t;
using PipelineLayoutHandle = uint32_t;
using SamplerHandle = uint32_t;
using DescriptorLayoutHandle = uint32_t;
using DescriptorPoolHandle = uint32_t;
using DescriptorSetHandle = uint32_t;
using CommandBufferHandle = void*;

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

enum class DescriptorType : uint8_t {
    Sampler,
    CombinedImageSampler,
    SampledImage,
    StorageImage,
    UniformTexelBuffer,
    StorageTexelBuffer,
    UniformBuffer,
    StorageBuffer,
    UniformBufferDynamic,
    StorageBufferDynamic,
    InputAttachment
};

enum class ShaderStage : uint32_t {
    Vertex = 1 << 0,
    Fragment = 1 << 4, // VK_SHADER_STAGE_FRAGMENT_BIT is 0x00000010
    Compute = 1 << 5,  // VK_SHADER_STAGE_COMPUTE_BIT is 0x00000020
    AllGraphics = Vertex | Fragment,
    All = 0x7FFFFFFF
};

inline ShaderStage operator|(ShaderStage a, ShaderStage b) {
    return static_cast<ShaderStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline ShaderStage operator&(ShaderStage a, ShaderStage b) {
    return static_cast<ShaderStage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

struct DescriptorSetLayoutBinding {
    uint32_t binding;
    DescriptorType descriptorType;
    uint32_t descriptorCount;
    ShaderStage stageFlags;
};

struct DescriptorLayoutDesc {
    const DescriptorSetLayoutBinding* bindings;
    uint32_t bindingCount;
};

struct PushConstantRange {
    ShaderStage stageFlags;
    uint32_t offset;
    uint32_t size;
};


enum class PolygonMode : uint8_t { Fill, Line };
enum class CullMode : uint8_t { None, Front, Back, FrontAndBack };
enum class FrontFace : uint8_t { CounterClockwise, Clockwise };
enum class CompareOp : uint8_t { Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always };
enum class Topology : uint8_t { TriangleList, LineList, PointList };
enum class VertexFormat : uint8_t { Float1, Float2, Float3, Float4, Int1 };

struct VertexInputBinding {
    uint32_t binding;
    uint32_t stride;
    bool isInstance;
};

struct VertexInputAttribute {
    uint32_t location;
    uint32_t binding;
    VertexFormat format;
    uint32_t offset;
};

struct GraphicsPipelineDesc {
    PipelineLayoutHandle layout;
    void* renderPass; // Opaque for now

    const void* vertexShaderCode;
    size_t vertexShaderSize;
    const void* fragmentShaderCode;
    size_t fragmentShaderSize;

    Topology topology = Topology::TriangleList;
    PolygonMode polygonMode = PolygonMode::Fill;
    CullMode cullMode = CullMode::Back;
    FrontFace frontFace = FrontFace::Clockwise;
    CompareOp depthCompareOp = CompareOp::LessOrEqual;
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    bool colorBlendEnable = false; // Additive blend or simple alpha blend?
    
    const VertexInputBinding* vertexBindings = nullptr;
    uint32_t vertexBindingCount = 0;
    const VertexInputAttribute* vertexAttributes = nullptr;
    uint32_t vertexAttributeCount = 0;
    
    const char* debugName = nullptr;
};

struct PipelineLayoutDesc {
    const DescriptorLayoutHandle* layouts;
    uint32_t layoutCount;
    const PushConstantRange* pushConstants;
    uint32_t pushConstantCount;
};

struct ComputePipelineDesc {
    PipelineLayoutHandle layout;
    const void* shaderCode;
    size_t shaderCodeSize;
    const char* entryPoint;
    const char* name;
};

struct DescriptorPoolSize {
    DescriptorType type;
    uint32_t descriptorCount;
};

struct DescriptorPoolDesc {
    const DescriptorPoolSize* poolSizes;
    uint32_t poolSizeCount;
    uint32_t maxSets;
};

struct DescriptorSetAllocateDesc {
    DescriptorPoolHandle pool;
    uint32_t setCount;
    const DescriptorLayoutHandle* layouts;
};

enum class TextureLayout : uint8_t {
    Undefined,
    General,
    ShaderReadOnlyOptimal
};

struct DescriptorImageInfo {
    SamplerHandle sampler;
    ImageViewHandle imageView; // If INVALID_HANDLE, texture's default view is used
    TextureHandle texture;
    TextureLayout imageLayout;
};

struct DescriptorBufferInfo {
    BufferHandle buffer;
    uint64_t offset;
    uint64_t range;
};

struct WriteDescriptorSet {
    DescriptorSetHandle dstSet;
    uint32_t dstBinding;
    uint32_t dstArrayElement;
    uint32_t descriptorCount;
    DescriptorType descriptorType;
    const DescriptorImageInfo* pImageInfo;
    const DescriptorBufferInfo* pBufferInfo;
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
    virtual bool DrawFrame() = 0;
    virtual void HandleInputs(const struct WindowOps* ops) = 0;

    // Resources
    virtual BufferHandle CreateBuffer(std::size_t size, BufferUsage usage, const void* initialData = nullptr, const char* name = nullptr) = 0;
    virtual void DestroyBuffer(BufferHandle handle) = 0;
    virtual void* MapBuffer(BufferHandle handle) = 0;
    virtual void UnmapBuffer(BufferHandle handle) = 0;

    virtual TextureHandle CreateTexture(uint32_t width, uint32_t height, TextureFormat format, TextureUsage usage, uint32_t mipLevels = 1, const char* name = nullptr) = 0;
    virtual void DestroyTexture(TextureHandle handle) = 0;

    virtual ImageViewHandle CreateImageView(TextureHandle texture, uint32_t baseMipLevel = 0, uint32_t levelCount = 1, uint32_t baseArrayLayer = 0, uint32_t layerCount = 1) = 0;
    virtual void DestroyImageView(ImageViewHandle handle) = 0;

    virtual SamplerHandle CreateSampler(uint32_t mipLevels = 1, bool clampToEdge = false, const char* name = nullptr) = 0;
    virtual void DestroySampler(SamplerHandle handle) = 0;

    virtual DescriptorLayoutHandle CreateDescriptorLayout(const DescriptorLayoutDesc& desc, const char* name = nullptr) = 0;
    virtual void DestroyDescriptorLayout(DescriptorLayoutHandle handle) = 0;

    virtual DescriptorPoolHandle CreateDescriptorPool(const DescriptorPoolDesc& desc, const char* name = nullptr) = 0;
    virtual void DestroyDescriptorPool(DescriptorPoolHandle handle) = 0;

    virtual bool AllocateDescriptorSets(const DescriptorSetAllocateDesc& desc, DescriptorSetHandle* outSets) = 0;

    virtual void UpdateDescriptorSets(uint32_t writeCount, const WriteDescriptorSet* pDescriptorWrites) = 0;

    virtual PipelineLayoutHandle CreatePipelineLayout(const PipelineLayoutDesc& desc, const char* name = nullptr) = 0;
    virtual void DestroyPipelineLayout(PipelineLayoutHandle handle) = 0;

    virtual PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc) = 0;
    virtual PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
    virtual void DestroyPipeline(PipelineHandle handle) = 0;

    // Frame lifecycle
    virtual SwapchainStatus AcquireNextImage(uint32_t* imageIndex) = 0;
    virtual void UpdateUBO(const struct UBOData& data) = 0;
    virtual bool BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual SwapchainStatus SubmitAndPresent(uint32_t imageIndex) = 0;

    // Render pass
    virtual void BeginRenderPass() = 0;
    virtual void EndRenderPass() = 0;
    virtual void CmdBindPipeline(CommandBufferHandle cb, PipelineHandle pipeline, bool isCompute = false) = 0;
    virtual void CmdBindDescriptorSets(CommandBufferHandle cb, PipelineLayoutHandle layout, uint32_t firstSet, uint32_t count, const DescriptorSetHandle* sets, bool isCompute = false) = 0;
    virtual void CmdPushConstants(CommandBufferHandle cb, PipelineLayoutHandle layout, ShaderStage stage, uint32_t offset, uint32_t size, const void* values) = 0;
    virtual void CmdDraw(CommandBufferHandle cb, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) = 0;
    virtual void CmdDrawIndexed(CommandBufferHandle cb, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) = 0;
    virtual void CmdDispatch(CommandBufferHandle cb, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;

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
