#include "vk_engine_bloom.h"
#include "app_log.h"
#include "rhi/vulkan_command_list.h"
#include "rhi/vulkan_rhi.h"
#include "tracy_client.h"
#include "tracy_vulkan.h"
#include "vk_engine.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void record_multipass_downsample(BloomPipeline* pipeline, VulkanEngine* engine, VkCommandBuffer cb, VulkanRHI* vkRhi) {
    SVK_TRACY_VK_ZONE_C(gpuDownZone, engine, cb, "Bloom Downsample", tracy_color::GpuCompute);
    VulkanCommandList cmd(vkRhi, cb);

    cmd.BindComputePipeline(pipeline->downsamplePipeline.get());

    for (int i = 0; i < kBloomMipCount; ++i) {
        TextureHandle srcTex = (i == 0) ? engine->swapchainMgr.colorAttachment.get() : pipeline->downMips[i - 1].texture.get();
        TextureHandle dstTex = pipeline->downMips[i].texture.get();

        cmd.TransitionTexture(srcTex, ResourceState::ComputeShaderRead);
        cmd.TransitionTexture(dstTex, ResourceState::ComputeShaderWrite);

        uint32_t srcW = (i == 0) ? engine->swapchainMgr.swapchainExtent.width : pipeline->downMips[i - 1].width;
        uint32_t srcH = (i == 0) ? engine->swapchainMgr.swapchainExtent.height : pipeline->downMips[i - 1].height;

        BloomDownsamplePushConstants pc{};
        pc.texelSizeX = 1.0f / static_cast<float>(srcW);
        pc.texelSizeY = 1.0f / static_cast<float>(srcH);
        pc.threshold = pipeline->threshold;
        pc.knee = pipeline->softKnee;
        pc.isPrefilter = (i == 0) ? 1 : 0;

        cmd.SetBindGroup(0, pipeline->downsampleBindGroups[i].get());
        cmd.PushComputeConstants(&pc, sizeof(BloomDownsamplePushConstants));

        uint32_t groupCountX = (pipeline->downMips[i].width + 7) / 8;
        uint32_t groupCountY = (pipeline->downMips[i].height + 7) / 8;
        cmd.DispatchCompute(groupCountX, groupCountY, 1);
    }
}

void record_upsample_chain(BloomPipeline* pipeline, VulkanEngine* engine, VkCommandBuffer cb, VulkanRHI* vkRhi) {
    SVK_TRACY_VK_ZONE_C(gpuUpZone, engine, cb, "Bloom Upsample", tracy_color::GpuCompute);
    VulkanCommandList cmd(vkRhi, cb);

    cmd.BindComputePipeline(pipeline->upsamplePipeline.get());

    for (int i = kBloomMipCount - 2; i >= 0; --i) {
        TextureHandle lowerMipTex = (i == kBloomMipCount - 2) ? pipeline->downMips[kBloomMipCount - 1].texture.get() : pipeline->upMips[i + 1].texture.get();
        TextureHandle currentMipTex = pipeline->downMips[i].texture.get();
        TextureHandle dstTex = pipeline->upMips[i].texture.get();

        cmd.TransitionTexture(lowerMipTex, ResourceState::ComputeShaderRead);
        cmd.TransitionTexture(currentMipTex, ResourceState::ComputeShaderRead);
        cmd.TransitionTexture(dstTex, ResourceState::ComputeShaderWrite);

        uint32_t lowerW = (i == kBloomMipCount - 2) ? pipeline->downMips[kBloomMipCount - 1].width : pipeline->upMips[i + 1].width;
        uint32_t lowerH = (i == kBloomMipCount - 2) ? pipeline->downMips[kBloomMipCount - 1].height : pipeline->upMips[i + 1].height;

        BloomUpsamplePushConstants pc{};
        pc.texelSizeX = 1.0f / static_cast<float>(lowerW);
        pc.texelSizeY = 1.0f / static_cast<float>(lowerH);
        pc.filterRadius = pipeline->filterRadius;
        pc.bloomWeight = 1.0f;

        cmd.SetBindGroup(0, pipeline->upsampleBindGroups[i].get());
        cmd.PushComputeConstants(&pc, sizeof(BloomUpsamplePushConstants));

        uint32_t groupCountX = (pipeline->upMips[i].width + 7) / 8;
        uint32_t groupCountY = (pipeline->upMips[i].height + 7) / 8;
        cmd.DispatchCompute(groupCountX, groupCountY, 1);
    }

    cmd.TransitionTexture(pipeline->upMips[0].texture.get(), ResourceState::ShaderResource);
    TextureHandle debugTex = pipeline->GetActiveDebugTexture(engine->appState->core.render.bloomDebugMode, engine->appState->core.render.bloomDebugMip);
    if (debugTex != pipeline->upMips[0].texture.get() && debugTex != INVALID_HANDLE) {
        cmd.TransitionTexture(debugTex, ResourceState::ShaderResource);
    }
    cmd.FlushBarriers();
}

TextureFormat determine_bloom_format(VkPhysicalDevice physicalDevice) {
    VkFormatProperties formatProps{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_B10G11R11_UFLOAT_PACK32, &formatProps);
    bool b10g11Supported =
        (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) && (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    if (b10g11Supported) {
        LOG_INFO("bloom", "Using high-efficiency B10G11R11_UFLOAT format for Bloom pyramid (-50%% memory bandwidth)");
        return TextureFormat::B10G11R11_UFLOAT;
    }
    LOG_WARNING("bloom", "B10G11R11_UFLOAT storage unsupported by hardware, falling back to RGBA16_SFLOAT");
    return TextureFormat::RGBA16_SFLOAT;
}

bool allocate_bloom_mips(BloomPipeline* pipeline, IRHI* rhi, TextureFormat bloomFormat, uint32_t baseWidth, uint32_t baseHeight, uint32_t baseShift) {
    for (int i = 0; i < kBloomMipCount; ++i) {
        uint32_t w = std::max(1u, baseWidth >> (i + baseShift));
        uint32_t h = std::max(1u, baseHeight >> (i + baseShift));

        pipeline->downMips[i].width = w;
        pipeline->downMips[i].height = h;
        char name[64];
        snprintf(name, sizeof(name), "Bloom_DownMip_%d", i);
        pipeline->downMips[i].texture.Reset(rhi, rhi->CreateTexture(w, h, bloomFormat, TextureUsage::Storage, 1, name));
        if (!pipeline->downMips[i].texture.is_valid()) {
            LOG_ERROR("bloom", "Failed to allocate texture for downsample mip %d", i);
            return false;
        }

        if (i < kBloomMipCount - 1) {
            pipeline->upMips[i].width = w;
            pipeline->upMips[i].height = h;
            snprintf(name, sizeof(name), "Bloom_UpMip_%d", i);
            pipeline->upMips[i].texture.Reset(rhi, rhi->CreateTexture(w, h, bloomFormat, TextureUsage::Storage, 1, name));
            if (!pipeline->upMips[i].texture.is_valid()) {
                LOG_ERROR("bloom", "Failed to allocate texture for upsample mip %d", i);
                return false;
            }
        }
    }
    return true;
}

bool create_downsample_bind_groups(BloomPipeline* pipeline, VulkanEngine* engine, IRHI* rhi) {
    BindGroupLayoutDesc downLayoutDesc{{
        {0, BindingType::CombinedImageSampler, 1, 0},
        {1, BindingType::StorageTexture, 1, 0},
    }};

    for (int i = 0; i < kBloomMipCount; ++i) {
        TextureHandle inputTexture = (i == 0) ? engine->swapchainMgr.colorAttachment.get() : pipeline->downMips[i - 1].texture.get();
        TextureHandle outputTexture = pipeline->downMips[i].texture.get();

        char bgName[64];
        snprintf(bgName, sizeof(bgName), "Bloom_Downsample_BG_%d", i);
        BindGroupDesc bgDesc{
            bgName,
            downLayoutDesc,
            {
                {0, BindingType::CombinedImageSampler, inputTexture, INVALID_HANDLE, pipeline->linearSampler.get(), INVALID_HANDLE, 0, 0},
                {1, BindingType::StorageTexture, outputTexture, INVALID_HANDLE, INVALID_HANDLE, INVALID_HANDLE, 0, 0},
            },
        };
        pipeline->downsampleBindGroups[i].Reset(rhi, rhi->CreateBindGroup(bgDesc));
        if (!pipeline->downsampleBindGroups[i].is_valid()) {
            LOG_ERROR("bloom", "Failed to create downsample bind group %d", i);
            return false;
        }
    }
    return true;
}

bool create_upsample_bind_groups(BloomPipeline* pipeline, IRHI* rhi) {
    BindGroupLayoutDesc upLayoutDesc{{
        {0, BindingType::CombinedImageSampler, 1, 0},
        {1, BindingType::CombinedImageSampler, 1, 0},
        {2, BindingType::StorageTexture, 1, 0},
    }};

    for (int i = kBloomMipCount - 2; i >= 0; --i) {
        TextureHandle lowerMipTex = (i == kBloomMipCount - 2) ? pipeline->downMips[kBloomMipCount - 1].texture.get() : pipeline->upMips[i + 1].texture.get();
        TextureHandle currentMipTex = pipeline->downMips[i].texture.get();
        TextureHandle outputTex = pipeline->upMips[i].texture.get();

        char bgName[64];
        snprintf(bgName, sizeof(bgName), "Bloom_Upsample_BG_%d", i);
        BindGroupDesc bgDesc{
            bgName,
            upLayoutDesc,
            {
                {0, BindingType::CombinedImageSampler, lowerMipTex, INVALID_HANDLE, pipeline->linearSampler.get(), INVALID_HANDLE, 0, 0},
                {1, BindingType::CombinedImageSampler, currentMipTex, INVALID_HANDLE, pipeline->linearSampler.get(), INVALID_HANDLE, 0, 0},
                {2, BindingType::StorageTexture, outputTex, INVALID_HANDLE, INVALID_HANDLE, INVALID_HANDLE, 0, 0},
            },
        };
        pipeline->upsampleBindGroups[i].Reset(rhi, rhi->CreateBindGroup(bgDesc));
        if (!pipeline->upsampleBindGroups[i].is_valid()) {
            LOG_ERROR("bloom", "Failed to create upsample bind group %d", i);
            return false;
        }
    }
    return true;
}

} // namespace

GfxResult BloomPipeline::Init(VulkanEngine* engine) {
    if (!engine || !engine->appState || !engine->appState->rhi) {
        return GfxResult::ErrorInitializationFailed;
    }
    _engine = engine;
    IRHI* rhi = engine->appState->rhi;

    // 1. Sampler bilinéaire clamp to edge
    linearSampler.Reset(rhi, rhi->CreateSampler(1, true, "Bloom_LinearSampler"));
    if (!linearSampler.is_valid()) {
        LOG_ERROR("bloom", "Failed to create linear sampler");
        return GfxResult::ErrorInitializationFailed;
    }

    // 2. Declarative Downsample & Upsample Compute Pipelines
    BindGroupLayoutDesc downLayoutDesc{{
        {0, BindingType::CombinedImageSampler, 1, 0},
        {1, BindingType::StorageTexture, 1, 0},
    }};
    DeclarativeComputePipelineDesc downCPDesc{
        "Bloom_Downsample_Pipeline", "shaders/bloom_downsample.spv", nullptr, 0, "main", {downLayoutDesc}, sizeof(BloomDownsamplePushConstants),
    };
    downsamplePipeline.Reset(rhi, rhi->CreateComputePipeline(downCPDesc));

    BindGroupLayoutDesc upLayoutDesc{{
        {0, BindingType::CombinedImageSampler, 1, 0},
        {1, BindingType::CombinedImageSampler, 1, 0},
        {2, BindingType::StorageTexture, 1, 0},
    }};
    DeclarativeComputePipelineDesc upCPDesc{
        "Bloom_Upsample_Pipeline", "shaders/bloom_upsample.spv", nullptr, 0, "main", {upLayoutDesc}, sizeof(BloomUpsamplePushConstants),
    };
    upsamplePipeline.Reset(rhi, rhi->CreateComputePipeline(upCPDesc));

    if (!downsamplePipeline.is_valid() || !upsamplePipeline.is_valid()) {
        LOG_ERROR("bloom", "Failed to create bloom compute pipelines");
        return GfxResult::ErrorInitializationFailed;
    }

    initialized = true;
    LOG_INFO("bloom", "Bloom compute pipeline initialized successfully");
    return GfxResult::Success;
}

void BloomPipeline::Cleanup() {
    for (auto& mip : downMips) {
        mip.texture.Reset();
        mip.width = 0;
        mip.height = 0;
    }
    for (auto& mip : upMips) {
        mip.texture.Reset();
        mip.width = 0;
        mip.height = 0;
    }
    for (auto& bg : downsampleBindGroups) {
        bg.Reset();
    }
    for (auto& bg : upsampleBindGroups) {
        bg.Reset();
    }

    downsamplePipeline.Reset();
    upsamplePipeline.Reset();
    linearSampler.Reset();
    initialized = false;
}

GfxResult BloomPipeline::RecreateTextures(VulkanEngine* engine, uint32_t baseWidth, uint32_t baseHeight) {
    if (!initialized || !engine || !engine->appState || !engine->appState->rhi) {
        return GfxResult::ErrorInitializationFailed;
    }
    IRHI* rhi = engine->appState->rhi;

    TextureFormat bloomFormat = determine_bloom_format(engine->ctx.physicalDevice);

    const char* envQuarter = std::getenv("SVK_BLOOM_QUARTER_RES");
    bool useQuarterRes = (envQuarter && (std::strcmp(envQuarter, "1") == 0 || std::strcmp(envQuarter, "ON") == 0 || std::strcmp(envQuarter, "true") == 0));
    uint32_t baseShift = useQuarterRes ? 2 : 1;
    if (useQuarterRes) {
        LOG_INFO("bloom", "Using Quarter-Res Start (Mip 0: %ux%u, -75%% texels)", baseWidth >> 2, baseHeight >> 2);
    }

    if (!allocate_bloom_mips(this, rhi, bloomFormat, baseWidth, baseHeight, baseShift)) {
        return GfxResult::ErrorInitializationFailed;
    }

    if (!create_downsample_bind_groups(this, engine, rhi)) {
        return GfxResult::ErrorInitializationFailed;
    }

    if (!create_upsample_bind_groups(this, rhi)) {
        return GfxResult::ErrorInitializationFailed;
    }

    LOG_INFO("bloom", "Bloom textures successfully recreated for %ux%u (%ux%u at Mip 0)", baseWidth, baseHeight, downMips[0].width, downMips[0].height);
    return GfxResult::Success;
}

void BloomPipeline::RecordPasses(VulkanEngine* engine, VkCommandBuffer cb) {
    if (!enabled || !initialized || !engine) {
        return;
    }
    VulkanRHI* vkRhi = static_cast<VulkanRHI*>(engine->appState->rhi);

    SVK_TRACY_ZONE_SCOPED_C("Bloom Compute Pass", tracy_color::CpuRecord);
    SVK_TRACY_VK_ZONE_C(gpuBloomZone, engine, cb, "GPU Bloom Pipeline", tracy_color::GpuCompute);
    vkRhi->BeginDebugLabel("Render_BloomCompute", 1.0f, 0.6f, 0.0f);

    record_multipass_downsample(this, engine, cb, vkRhi);
    record_upsample_chain(this, engine, cb, vkRhi);

    vkRhi->EndDebugLabel();
}

TextureHandle BloomPipeline::GetActiveDebugTexture(int32_t debugMode, int32_t debugMip) const {
    if (debugMode == 2) {
        return downMips[0].texture.get();
    }
    if (debugMode == 3) {
        int mip = std::clamp(debugMip, 0, kBloomMipCount - 1);
        return downMips[mip].texture.get();
    }
    if (debugMode == 4) {
        if (debugMip >= kBloomMipCount - 1) {
            return downMips[kBloomMipCount - 1].texture.get();
        }
        int mip = std::clamp(debugMip, 0, kBloomMipCount - 2);
        return upMips[mip].texture.get();
    }
    return upMips[0].texture.get();
}
