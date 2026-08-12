#ifndef VK_ENGINE_IBL_H
#define VK_ENGINE_IBL_H

#include "rhi/rhi_ptr.h"
#include <chrono>
#include <vector>
#include <vma/vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct VulkanEngine;

// IBL Resolution Constants
constexpr uint32_t IBL_IRM_SIZE = 64;
constexpr uint32_t IBL_SPM_SIZE = 1024;
constexpr uint32_t IBL_SPM_MIPS = 11;
constexpr uint32_t IBL_BRDF_SIZE = 512;

// Luminance Reduction Constants
constexpr uint32_t IBL_LUM_WG_SIZE = 16;
constexpr uint32_t IBL_MAX_GROUPS = 65536;

enum class IblBakeState : uint8_t {
    Idle,
    UploadHdr,
    UploadHdrWait,
    GenerateMipmap,
    GenerateMipmapWait,
    Luminance,
    LuminanceWait,
    Brdf,
    BrdfWait,
    Irradiance,
    IrradianceWait,
    Prefilter,
    PrefilterWait,
    Finalize
};

class IblBaker {
  public:
    GfxResult Init(VulkanEngine* engine);
    void Cleanup();
    void cleanupPendingResources(VulkanEngine* engine);
    void ExportMaps(VulkanEngine* engine) const;

    // Core members from old IblResources
    rhi::TexturePtr irradianceMap;
    rhi::SamplerPtr irradianceSampler;

    rhi::TexturePtr prefilteredMap;
    rhi::SamplerPtr prefilteredSampler;

    rhi::TexturePtr brdfLut;
    rhi::SamplerPtr brdfLutSampler;

    // Internal Compute Resources (Luminance Reduction)
    VkBuffer lumGroupSumsBuffer{VK_NULL_HANDLE};
    VmaAllocation lumGroupSumsAllocation{VK_NULL_HANDLE};
    VkBuffer lumMeanBuffer{VK_NULL_HANDLE};
    VmaAllocation lumMeanAllocation{VK_NULL_HANDLE};

    // Compute Pipelines
    rhi::PipelinePtr irmapPipeline;
    rhi::PipelinePtr spmapPipeline;
    rhi::PipelinePtr brdfLutPipeline;
    rhi::PipelinePtr lum1Pipeline;
    rhi::PipelinePtr lum2Pipeline;

    rhi::PipelineLayoutPtr iblPipelineLayout;
    rhi::PipelineLayoutPtr lum1PipelineLayout;
    rhi::PipelineLayoutPtr lum2PipelineLayout;
    rhi::DescriptorLayoutPtr iblDescriptorSetLayout;
    rhi::DescriptorLayoutPtr lum1DescriptorSetLayout;
    rhi::DescriptorLayoutPtr lum2DescriptorSetLayout;
    rhi::DescriptorPoolPtr computeDescriptorPool;

    // Descriptor sets for individual compute passes
    DescriptorSetHandle lum1DescriptorSet{INVALID_HANDLE};
    DescriptorSetHandle lum2DescriptorSet{INVALID_HANDLE};
    DescriptorSetHandle irmapDescriptorSet{INVALID_HANDLE};
    DescriptorSetHandle spmapDescriptorSet{INVALID_HANDLE};
    DescriptorSetHandle brdfLutDescriptorSet{INVALID_HANDLE};

    bool brdfLutBaked{false};
    float bakedMeanLuminance{1.0f};

    // State machine & Slicing tracking
    std::chrono::high_resolution_clock::time_point envmapRequestTime;
    IblBakeState bakeState{IblBakeState::Idle};
    int currentSlice{0};
    int totalSlices{0};
    int currentMip{0};
    VkBuffer currentStagingBuffer{VK_NULL_HANDLE};

    VkFence iblBakeFence{VK_NULL_HANDLE};
    VkCommandBuffer iblBakeCommandBuffer{VK_NULL_HANDLE};
    std::vector<uint64_t> pendingDescriptorPools;
    std::vector<uint64_t> pendingImageViews;

    std::vector<rhi::TexturePtr> pendingOldTextures;
    std::vector<rhi::SamplerPtr> pendingOldSamplers;
    std::vector<VkBuffer> pendingStagingBuffers;
    std::vector<VmaAllocation> pendingStagingAllocations;

    mutable std::vector<float> m_exportFloatData;
    mutable std::vector<float> m_exportFinalData;
};

void vk_ibl_bake_luminance(VulkanEngine* engine);
void vk_ibl_bake_brdf(VulkanEngine* engine);
void vk_ibl_bake_irradiance(VulkanEngine* engine);
void vk_ibl_bake_prefilter(VulkanEngine* engine);
void vk_ibl_reset_bake_fence(VulkanEngine* engine);

#endif // VK_ENGINE_IBL_H
