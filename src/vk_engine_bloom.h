#ifndef VK_ENGINE_BLOOM_H
#define VK_ENGINE_BLOOM_H

#include "result.h"
#include "rhi/rhi_ptr.h"
#include <array>
#include <cstdint>
#include <vulkan/vulkan.h>

struct VulkanEngine;

constexpr int kBloomMipCount = 5;

struct BloomDownsamplePushConstants {
    float texelSizeX;
    float texelSizeY;
    float threshold;
    float knee;
    int32_t isPrefilter;
    float _padding[3];
};

struct BloomUpsamplePushConstants {
    float texelSizeX;
    float texelSizeY;
    float filterRadius;
    float bloomWeight;
};

struct BloomMipLevel {
    rhi::TexturePtr texture;
    uint32_t width{0};
    uint32_t height{0};
};

class BloomPipeline {
  public:
    GfxResult Init(VulkanEngine* engine);
    void Cleanup();
    GfxResult RecreateTextures(VulkanEngine* engine, uint32_t baseWidth, uint32_t baseHeight);
    void RecordPasses(VulkanEngine* engine, VkCommandBuffer cb);
    TextureHandle GetActiveDebugTexture(int32_t debugMode, int32_t debugMip) const;

    // Compute pipeline resources
    rhi::PipelinePtr downsamplePipeline;
    rhi::PipelinePtr upsamplePipeline;
    rhi::SamplerPtr linearSampler;

    std::array<rhi::BindGroupPtr, kBloomMipCount> downsampleBindGroups{};
    std::array<rhi::BindGroupPtr, kBloomMipCount - 1> upsampleBindGroups{};

    std::array<BloomMipLevel, kBloomMipCount> downMips{};
    std::array<BloomMipLevel, kBloomMipCount - 1> upMips{};

    bool initialized{false};
    bool enabled{true};
    float threshold{1.0f};
    float softKnee{0.5f};
    float intensity{0.08f};
    float filterRadius{1.0f};

  private:
    VulkanEngine* _engine{nullptr};
};

#endif // VK_ENGINE_BLOOM_H
