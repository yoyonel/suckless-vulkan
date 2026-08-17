#ifndef VK_ENGINE_AUTOEXPOSURE_H
#define VK_ENGINE_AUTOEXPOSURE_H

#include "result.h"
#include "rhi/rhi_ptr.h"
#include <cstdint>
#include <vulkan/vulkan.h>

struct VulkanEngine;

struct AutoExposureHistogramPushConstants {
    float minLogLum{-8.0f};
    float invLogLumRange{1.0f / 16.0f};
    uint32_t width{0};
    uint32_t height{0};
};

struct AutoExposureAdaptPushConstants {
    float minLogLum{-8.0f};
    float logLumRange{16.0f};
    float minPercentile{0.05f};
    float maxPercentile{0.98f};
    float speedUp{2.0f};
    float speedDown{1.0f};
    float deltaTime{0.016f};
    float keyValue{0.18f};
    float minExposure{0.05f};
    float maxExposure{10.0f};
};

class AutoExposurePipeline {
  public:
    GfxResult Init(VulkanEngine* engine);
    void Cleanup();
    void UpdateSceneDescriptor(VulkanEngine* engine, TextureHandle sceneHdrTexture);
    void RecordPasses(VulkanEngine* engine, VkCommandBuffer cb, TextureHandle sceneHdrTexture, uint32_t width, uint32_t height, float deltaTime);

    TextureHandle GetExposureTexture() const {
        return exposureTexture.get();
    }
    BufferHandle GetDebugHistogramBuffer() const {
        return debugHistogramBuffer.get();
    }

    // Compute pipeline resources
    rhi::PipelinePtr histogramPipeline;
    rhi::PipelinePtr adaptPipeline;

    rhi::BindGroupPtr histogramBindGroup;
    rhi::BindGroupPtr adaptBindGroup;

    rhi::BufferPtr histogramBuffer;
    rhi::TexturePtr exposureTexture;
    rhi::BufferPtr debugHistogramBuffer;
    rhi::SamplerPtr pointSampler;

    TextureHandle currentSceneHdrTexture{INVALID_HANDLE};

    bool initialized{false};
    bool enabled{false};
    bool debugOverlay{false};

    float minLogLum{-8.0f};
    float maxLogLum{8.0f};
    float minPercentile{0.05f};
    float maxPercentile{0.98f};
    float speedUp{2.0f};
    float speedDown{1.0f};
    float keyValue{0.18f};
    float minExposure{0.05f};
    float maxExposure{10.0f};

  private:
    VulkanEngine* _engine{nullptr};
};

#endif // VK_ENGINE_AUTOEXPOSURE_H
