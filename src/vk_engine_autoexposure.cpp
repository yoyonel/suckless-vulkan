#include "vk_engine_autoexposure.h"
#include "app_log.h"
#include "rhi/vulkan_command_list.h"
#include "rhi/vulkan_rhi.h"
#include "tracy_client.h"
#include "tracy_vulkan.h"
#include "vk_engine.h"
#include <algorithm>

GfxResult AutoExposurePipeline::Init(VulkanEngine* engine) {
    if (!engine || !engine->appState || !engine->appState->rhi) {
        return GfxResult::ErrorInitializationFailed;
    }
    _engine = engine;
    IRHI* rhi = engine->appState->rhi;

    // 1. Sampler
    pointSampler.Reset(rhi, rhi->CreateSampler(1, false, "AutoExposure_PointSampler"));
    if (!pointSampler.is_valid()) {
        LOG_ERROR("autoexposure", "Failed to create sampler");
        return GfxResult::ErrorInitializationFailed;
    }

    // 2. Buffers & Textures
    uint32_t zeroHist[64] = {0};
    histogramBuffer.Reset(rhi, rhi->CreateBuffer(sizeof(zeroHist), BufferUsage::Storage, zeroHist, "AutoExposure_Histogram_SSBO"));
    debugHistogramBuffer.Reset(rhi, rhi->CreateBuffer(sizeof(zeroHist), BufferUsage::Storage, zeroHist, "AutoExposure_DebugHistogram_SSBO"));
    exposureTexture.Reset(rhi, rhi->CreateTexture(1, 1, TextureFormat::RGBA32_SFLOAT, TextureUsage::Storage, 1, "AutoExposure_Texture_1x1"));

    if (!histogramBuffer.is_valid() || !debugHistogramBuffer.is_valid() || !exposureTexture.is_valid()) {
        LOG_ERROR("autoexposure", "Failed to allocate autoexposure buffers or textures");
        return GfxResult::ErrorInitializationFailed;
    }

    // 3. Declarative Compute Pipelines
    BindGroupLayoutDesc histLayoutDesc{{
        {0, BindingType::CombinedImageSampler, 1, 0},
        {1, BindingType::StorageBuffer, 1, 0},
    }};
    DeclarativeComputePipelineDesc histDesc{
        "AutoExposure_Histogram", "shaders/autoexposure_histogram.spv", nullptr, 0, "main", {histLayoutDesc}, sizeof(AutoExposureHistogramPushConstants),
    };
    histogramPipeline.Reset(rhi, rhi->CreateComputePipeline(histDesc));

    BindGroupLayoutDesc adaptLayoutDesc{{
        {0, BindingType::StorageBuffer, 1, 0},
        {1, BindingType::StorageTexture, 1, 0},
        {2, BindingType::StorageBuffer, 1, 0},
    }};
    DeclarativeComputePipelineDesc adaptDesc{
        "AutoExposure_Adapt", "shaders/autoexposure_adapt.spv", nullptr, 0, "main", {adaptLayoutDesc}, sizeof(AutoExposureAdaptPushConstants),
    };
    adaptPipeline.Reset(rhi, rhi->CreateComputePipeline(adaptDesc));

    if (!histogramPipeline.is_valid() || !adaptPipeline.is_valid()) {
        LOG_ERROR("autoexposure", "Failed to create autoexposure compute pipelines");
        return GfxResult::ErrorInitializationFailed;
    }

    // 4. Adapt BindGroup (immutable across frames)
    BindGroupDesc adaptBgDesc{
        "AutoExposure_Adapt_BindGroup",
        adaptLayoutDesc,
        {
            {0, BindingType::StorageBuffer, INVALID_HANDLE, INVALID_HANDLE, INVALID_HANDLE, histogramBuffer.get(), 0, sizeof(zeroHist)},
            {1, BindingType::StorageTexture, exposureTexture.get(), INVALID_HANDLE, INVALID_HANDLE, INVALID_HANDLE, 0, 0},
            {2, BindingType::StorageBuffer, INVALID_HANDLE, INVALID_HANDLE, INVALID_HANDLE, debugHistogramBuffer.get(), 0, sizeof(zeroHist)},
        },
    };
    adaptBindGroup.Reset(rhi, rhi->CreateBindGroup(adaptBgDesc));
    if (!adaptBindGroup.is_valid()) {
        LOG_ERROR("autoexposure", "Failed to create adapt bind group");
        return GfxResult::ErrorInitializationFailed;
    }

    initialized = true;
    LOG_INFO("autoexposure", "AutoExposure 64-bin histogram compute pipeline initialized successfully");
    return GfxResult::Success;
}

void AutoExposurePipeline::Cleanup() {
    histogramBuffer.Reset();
    debugHistogramBuffer.Reset();
    exposureTexture.Reset();
    histogramBindGroup.Reset();
    adaptBindGroup.Reset();
    histogramPipeline.Reset();
    adaptPipeline.Reset();
    pointSampler.Reset();
    currentSceneHdrTexture = INVALID_HANDLE;
    initialized = false;
}

void AutoExposurePipeline::UpdateSceneDescriptor(VulkanEngine* engine, TextureHandle sceneHdrTexture) {
    if (!initialized || !engine || !engine->appState || !engine->appState->rhi) {
        return;
    }
    if (sceneHdrTexture == currentSceneHdrTexture && histogramBindGroup.is_valid()) {
        return;
    }
    currentSceneHdrTexture = sceneHdrTexture;
    IRHI* rhi = engine->appState->rhi;

    uint32_t zeroHist[64] = {0};
    BindGroupLayoutDesc histLayoutDesc{{
        {0, BindingType::CombinedImageSampler, 1, 0},
        {1, BindingType::StorageBuffer, 1, 0},
    }};

    BindGroupDesc histBgDesc{
        "AutoExposure_Histogram_BindGroup",
        histLayoutDesc,
        {
            {0, BindingType::CombinedImageSampler, sceneHdrTexture, INVALID_HANDLE, pointSampler.get(), INVALID_HANDLE, 0, 0},
            {1, BindingType::StorageBuffer, INVALID_HANDLE, INVALID_HANDLE, INVALID_HANDLE, histogramBuffer.get(), 0, sizeof(zeroHist)},
        },
    };
    histogramBindGroup.Reset(rhi, rhi->CreateBindGroup(histBgDesc));
}

void AutoExposurePipeline::RecordPasses(VulkanEngine* engine, VkCommandBuffer cb, TextureHandle sceneHdrTexture, uint32_t width, uint32_t height,
                                        float deltaTime) {
    if (!initialized || !engine || (!enabled && !debugOverlay)) {
        return;
    }
    VulkanRHI* vkRhi = static_cast<VulkanRHI*>(engine->appState->rhi);
    VulkanCommandList cmd(vkRhi, cb);

    vkRhi->BeginDebugLabel("Render_AutoExposure", 0.2f, 0.8f, 1.0f);

    // 1. Histogram Dispatch
    {
        SVK_TRACY_VK_ZONE_C(gpuHistZone, engine, cb, "AutoExposure Histogram", tracy_color::GpuCompute);
        cmd.BeginDebugLabel("AutoExposure_Histogram", 0.3f, 0.7f, 0.9f);
        UpdateSceneDescriptor(engine, sceneHdrTexture);

        cmd.TransitionTexture(sceneHdrTexture, ResourceState::ComputeShaderRead);
        cmd.TransitionBuffer(histogramBuffer.get(), ResourceState::ComputeShaderWrite);

        cmd.BindComputePipeline(histogramPipeline.get());

        AutoExposureHistogramPushConstants pc{};
        pc.minLogLum = minLogLum;
        pc.invLogLumRange = 1.0f / std::max(maxLogLum - minLogLum, 1.0f);
        pc.width = width;
        pc.height = height;

        cmd.SetBindGroup(0, histogramBindGroup.get());
        cmd.PushComputeConstants(&pc, sizeof(AutoExposureHistogramPushConstants));

        uint32_t groupCountX = (width + 15) / 16;
        uint32_t groupCountY = (height + 15) / 16;
        cmd.DispatchCompute(groupCountX, groupCountY, 1);
        cmd.EndDebugLabel();
    }

    // 2. Adapt & Resolve Dispatch
    {
        SVK_TRACY_VK_ZONE_C(gpuAdaptZone, engine, cb, "AutoExposure Adapt", tracy_color::GpuCompute);
        cmd.BeginDebugLabel("AutoExposure_Adapt", 0.1f, 0.9f, 0.6f);

        cmd.TransitionBuffer(histogramBuffer.get(), ResourceState::ComputeShaderRead);
        cmd.TransitionTexture(exposureTexture.get(), ResourceState::ComputeShaderWrite);
        cmd.TransitionBuffer(debugHistogramBuffer.get(), ResourceState::ComputeShaderWrite);

        cmd.BindComputePipeline(adaptPipeline.get());

        AutoExposureAdaptPushConstants pc{};
        pc.minLogLum = minLogLum;
        pc.logLumRange = std::max(maxLogLum - minLogLum, 1.0f);
        pc.minPercentile = minPercentile;
        pc.maxPercentile = maxPercentile;
        pc.speedUp = speedUp;
        pc.speedDown = speedDown;
        pc.deltaTime = deltaTime;
        pc.keyValue = keyValue;
        pc.minExposure = minExposure;
        pc.maxExposure = maxExposure;

        cmd.SetBindGroup(0, adaptBindGroup.get());
        cmd.PushComputeConstants(&pc, sizeof(AutoExposureAdaptPushConstants));

        cmd.DispatchCompute(1, 1, 1);

        // Transition outputs for downstream consumers (Tone Mapping & Debug Overlay in Fragment shader)
        cmd.TransitionTexture(exposureTexture.get(), ResourceState::ShaderResource);
        cmd.TransitionBuffer(debugHistogramBuffer.get(), ResourceState::ShaderResource);
        cmd.FlushBarriers();
        cmd.EndDebugLabel();
    }

    vkRhi->EndDebugLabel();
}
