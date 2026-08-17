#ifndef RHI_TYPES_H
#define RHI_TYPES_H

#include <cstdint>
#include <vector>

enum class ResourceState : uint16_t {
    Undefined          = 0,
    Common             = 1 << 0,
    VertexBuffer       = 1 << 1,
    IndexBuffer        = 1 << 2,
    UniformBuffer      = 1 << 3,
    ShaderResource     = 1 << 4, // Lecture Fragment/Vertex (SHADER_READ_ONLY_OPTIMAL)
    ShaderRead         = 1 << 4, // Alias for ShaderResource / generic read
    ComputeShaderRead  = 1 << 5, // Lecture Compute Sampler (SHADER_READ_ONLY_OPTIMAL)
    ComputeShaderWrite = 1 << 6, // Écriture Storage Image / SSBO (GENERAL)
    ComputeWrite       = 1 << 6, // Alias for ComputeShaderWrite
    ComputeReadWrite   = (1 << 5) | (1 << 6),
    RenderTarget       = 1 << 7, // Color Attachment (COLOR_ATTACHMENT_OPTIMAL)
    DepthStencilRead   = 1 << 8, // Depth Read Only (DEPTH_STENCIL_READ_ONLY_OPTIMAL)
    DepthStencilWrite  = 1 << 9, // Depth Write (DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    TransferSrc        = 1 << 10,
    TransferDst        = 1 << 11,
    TransferRead       = 1 << 10, // Alias for TransferSrc
    TransferWrite      = 1 << 11, // Alias for TransferDst
    Present            = 1 << 12
};

constexpr ResourceState operator|(ResourceState a, ResourceState b) {
    return static_cast<ResourceState>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

constexpr ResourceState operator&(ResourceState a, ResourceState b) {
    return static_cast<ResourceState>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
}

constexpr ResourceState operator^(ResourceState a, ResourceState b) {
    return static_cast<ResourceState>(static_cast<uint16_t>(a) ^ static_cast<uint16_t>(b));
}

constexpr ResourceState operator~(ResourceState a) {
    return static_cast<ResourceState>(~static_cast<uint16_t>(a));
}

inline ResourceState& operator|=(ResourceState& a, ResourceState b) {
    a = a | b;
    return a;
}

inline ResourceState& operator&=(ResourceState& a, ResourceState b) {
    a = a & b;
    return a;
}

inline ResourceState& operator^=(ResourceState& a, ResourceState b) {
    a = a ^ b;
    return a;
}

// Declarative BindGroups & Compute Pipelines (Phase 2)

enum class BindingType : uint8_t {
    Sampler,
    SampledTexture,
    CombinedImageSampler,
    StorageTexture,
    UniformBuffer,
    UniformBufferDynamic,
    StorageBuffer,
    StorageBufferDynamic,
    InputAttachment
};

struct BindGroupLayoutEntry {
    uint32_t binding{0};
    BindingType type{BindingType::SampledTexture};
    uint32_t count{1};
    uint32_t stageFlags{0}; // 0 defaults to ShaderStage::Compute in compute context
};

struct BindGroupLayoutDesc {
    std::vector<BindGroupLayoutEntry> bindings;
};

struct BindGroupEntry {
    uint32_t binding{0};
    BindingType type{BindingType::SampledTexture};
    uint32_t texture{0xFFFFFFFF};   // TextureHandle
    uint32_t imageView{0xFFFFFFFF}; // ImageViewHandle
    uint32_t sampler{0xFFFFFFFF};   // SamplerHandle
    uint32_t buffer{0xFFFFFFFF};    // BufferHandle
    uint64_t offset{0};
    uint64_t size{0};               // 0 = whole buffer
};

struct BindGroupDesc {
    const char* debugName{nullptr};
    BindGroupLayoutDesc layout;
    std::vector<BindGroupEntry> entries;
};

struct DeclarativeComputePipelineDesc {
    const char* debugName{nullptr};
    const char* shaderPath{nullptr};
    const void* shaderCode{nullptr};
    size_t shaderCodeSize{0};
    const char* entryPoint{"main"};
    std::vector<BindGroupLayoutDesc> bindGroupLayouts;
    uint32_t pushConstantsSize{0};
};

// Declarative Graphics Pipelines (Phase 3 & 4)

enum class PolygonMode : uint8_t { Fill, Line };
enum class CullMode : uint8_t { None, Front, Back, FrontAndBack };
enum class FrontFace : uint8_t { CounterClockwise, Clockwise };
enum class CompareOp : uint8_t { Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always };
enum class Topology : uint8_t { TriangleList, LineList, PointList };
enum class VertexFormat : uint8_t { Float1, Float2, Float3, Float4, Int1, UInt1 };

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

struct DeclarativeGraphicsPipelineDesc {
    const char* debugName{nullptr};
    const char* vertexShaderPath{nullptr};
    const void* vertexShaderCode{nullptr};
    size_t vertexShaderSize{0};
    const char* fragmentShaderPath{nullptr};
    const void* fragmentShaderCode{nullptr};
    size_t fragmentShaderSize{0};

    void* renderPass{nullptr};
    uint32_t subpass{0};

    std::vector<BindGroupLayoutDesc> bindGroupLayouts;

    Topology topology{Topology::TriangleList};
    PolygonMode polygonMode{PolygonMode::Fill};
    CullMode cullMode{CullMode::Back};
    FrontFace frontFace{FrontFace::Clockwise};
    CompareOp depthCompareOp{CompareOp::LessOrEqual};
    bool depthTestEnable{true};
    bool depthWriteEnable{true};
    bool colorBlendEnable{false};

    std::vector<VertexInputBinding> vertexBindings;
    std::vector<VertexInputAttribute> vertexAttributes;

    uint32_t pushConstantsSize{0};
    uint32_t pushConstantStages{0}; // 0 defaults to Vertex | Fragment
};

#endif // RHI_TYPES_H
