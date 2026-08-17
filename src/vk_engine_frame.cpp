#include "vk_engine_frame.h"
#include "rhi/command_list.h"

#include "runtime_controls.h"
#include "tracy_client.h"
#include "tracy_vulkan.h"
#include "vk_engine_envmap.h"
#include "vk_engine_init.h"
#include "vk_engine_runtime.h"

#include "rhi/render_graph.h"
#include "rhi/vulkan_command_list.h"
#include "rhi/vulkan_rhi.h"

#include "app_log.h"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace {

constexpr uint32_t kGridSize = 10;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 1000.0f;
constexpr float kLegacyDefaultFov = 60.0f;

} // namespace

static void record_secondary_forward_pass(VulkanEngine* engine, CoreEngine& core, VulkanRHI* rhi, uint32_t slot) {
    VkCommandBufferInheritanceInfo inheritanceInfo{};
    inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    inheritanceInfo.renderPass = engine->renderPass;
    inheritanceInfo.subpass = 0;
    inheritanceInfo.framebuffer = engine->swapchainMgr.colorFramebuffer;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = &inheritanceInfo;

    VkCommandBuffer secCb = engine->secondaryForwardCb[slot];
    vkBeginCommandBuffer(secCb, &beginInfo);

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = (float)engine->swapchainMgr.swapchainExtent.width;
    vp.height = (float)engine->swapchainMgr.swapchainExtent.height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(secCb, 0, 1, &vp);

    VkRect2D sc{};
    sc.offset = {0, 0};
    sc.extent = engine->swapchainMgr.swapchainExtent;
    vkCmdSetScissor(secCb, 0, 1, &sc);

    VulkanCommandList secCmdList(rhi, secCb);
    constexpr size_t kUboStride = (sizeof(UBOData) + 255) & ~255;
    uint32_t dynamicOffset = static_cast<uint32_t>(slot * kUboStride);

    if (core.render.showEnvmap) {
        secCmdList.BindGraphicsPipeline(engine->skyboxPipeline.get());
        secCmdList.SetGraphicsBindGroup(0, engine->globalBindGroup.get(), 1, &dynamicOffset);
        secCmdList.Draw(3, 1, 0, 0);
    }

    if (!core.render.billboardMode) {
        secCmdList.BindGraphicsPipeline(core.render.wireframeMode ? engine->wireframePipeline.get() : engine->graphicsPipeline.get());
        secCmdList.SetGraphicsBindGroup(0, engine->globalBindGroup.get(), 1, &dynamicOffset);
        rhi->BindMeshBuffers(&secCmdList, false);
        secCmdList.DrawIndexed(engine->indexCount, kGridSize * kGridSize, 0, 0, 0);
    }

    vkEndCommandBuffer(secCb);
}

static void record_forward_pass(IRenderCommandList* cmdList, VulkanEngine* engine, CoreEngine& core, IRHI* rhi) {
    SVK_RHI_GPU_ZONE_C(gpuFrameZone, rhi, "Forward Pass", tracy_color::GpuPass);

    rhi->BeginDebugLabel("Render_ForwardPass", 1.0f, 0.5f, 0.0f);

    if (core.render.billboardMode) {
        cmdList->BeginRenderPass();

        if (core.render.showEnvmap) {
            cmdList->BindGraphicsPipeline(engine->skyboxPipeline.get());
            rhi->BindGlobalDescriptor(cmdList);
            cmdList->Draw(3, 1, 0, 0);
        }

        const glm::vec3 camPos = core.camera.position;
        BillboardSoA* soa = &core.scene.billboardSoA;

        struct alignas(8) BillboardSortItem {
            uint32_t distBits;
            uint32_t index;
        };

        BillboardSortItem* sortItems = static_cast<BillboardSortItem*>(arena_alloc(&tls_scratch.arena, soa->count * sizeof(BillboardSortItem), 8));
        const glm::vec4* __restrict posArray = static_cast<const glm::vec4*>(__builtin_assume_aligned(soa->pos, 64));
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC unroll 4
#endif
        for (int i = 0; i < soa->count; ++i) {
            __builtin_prefetch(&posArray[i + 8], 0, 1);
            float dx = posArray[i].x - camPos.x;
            float dy = posArray[i].y - camPos.y;
            float dz = posArray[i].z - camPos.z;
            float distSq = (dx * dx) + (dy * dy) + (dz * dz);
            std::memcpy(&sortItems[i].distBits, &distSq, sizeof(uint32_t));
            sortItems[i].index = static_cast<uint32_t>(i);
        }

        std::sort(sortItems, sortItems + soa->count, [](const BillboardSortItem& a, const BillboardSortItem& b) { return a.distBits > b.distBits; });

        uint32_t* tempIndices = static_cast<uint32_t*>(arena_alloc(&tls_scratch.arena, soa->count * sizeof(uint32_t), 4));
        for (int i = 0; i < soa->count; ++i) {
            tempIndices[i] = sortItems[i].index;
        }

        rhi->UpdateBillboardInstances(tempIndices, soa->count);

        cmdList->BindGraphicsPipeline(engine->billboardPipeline.get());
        rhi->BindGlobalDescriptor(cmdList);
        rhi->BindMeshBuffers(cmdList, true);
        cmdList->Draw(6, static_cast<uint32_t>(soa->count), 0, 0);

        cmdList->EndRenderPass();
        rhi->EndDebugLabel();
        return;
    }

    VulkanRHI* vkRhi = static_cast<VulkanRHI*>(rhi);
    if (!engine->secondaryForwardRecorded) {
        for (uint32_t s = 0; s < 3; ++s) {
            record_secondary_forward_pass(engine, core, vkRhi, s);
        }
        engine->secondaryForwardRecorded = true;
    }

    VkClearValue cl[2] = {};
    cl[0].color = {{0.05f, 0.05f, 0.2f, 1.0f}};
    cl[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = engine->renderPass;
    rp.framebuffer = engine->swapchainMgr.colorFramebuffer;
    rp.renderArea.extent = engine->swapchainMgr.swapchainExtent;
    rp.clearValueCount = 2;
    rp.pClearValues = cl;

    VkCommandBuffer primaryCb = engine->commandBuffer;
    vkCmdBeginRenderPass(primaryCb, &rp, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

    uint32_t slot = engine->currentFrameIndex % 3;
    vkCmdExecuteCommands(primaryCb, 1, &engine->secondaryForwardCb[slot]);

    vkCmdEndRenderPass(primaryCb);
    rhi->EndDebugLabel();
}

static void record_debug_pass(IRenderCommandList* cmdList, VulkanEngine* engine, CoreEngine& core, IRHI* rhi) {
    if (!core.render.wireframeMode || !core.render.billboardMode)
        return;

    SVK_RHI_GPU_ZONE_C(gpuDebugZone, rhi, "Debug Pass", tracy_color::GpuPassDebug);
    rhi->BeginDebugLabel("Render_DebugPass", 0.0f, 1.0f, 0.4f);
    cmdList->BeginRenderPassLoad();

    constexpr size_t kUboStride = (sizeof(UBOData) + 255) & ~255;
    uint32_t dynamicOffset = static_cast<uint32_t>((engine->currentFrameIndex % 3) * kUboStride);

    DebugPushConstant dp = {};
    dp.model = glm::mat4(1.0f);
    dp.radius = 1.0f;

    cmdList->BindGraphicsPipeline(engine->debugTrianglePipeline.get());
    cmdList->SetGraphicsBindGroup(0, engine->globalBindGroup.get(), 1, &dynamicOffset);
    dp.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.1f);
    dp.mode = 1;
    dp.stippled = 2;
    cmdList->PushGraphicsConstants(ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(DebugPushConstant), &dp);
    cmdList->Draw(6, kGridSize * kGridSize, 0, 0);

    cmdList->BindGraphicsPipeline(engine->debugLinePipeline.get());
    cmdList->SetGraphicsBindGroup(0, engine->globalBindGroup.get(), 1, &dynamicOffset);
    dp.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    dp.mode = 1;
    dp.stippled = 0;
    cmdList->PushGraphicsConstants(ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(DebugPushConstant), &dp);
    cmdList->Draw(8, kGridSize * kGridSize, 0, 0);

    dp.color = glm::vec4(1.0f, 1.0f, 0.0f, 0.5f);
    dp.mode = 0;
    dp.stippled = 1;
    cmdList->PushGraphicsConstants(ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(DebugPushConstant), &dp);
    cmdList->Draw(24, kGridSize * kGridSize, 0, 0);

    cmdList->EndRenderPass();
    rhi->EndDebugLabel();
}

static void record_fused_pass(VkCommandBuffer cb, VulkanEngine* engine, CoreEngine& core, VulkanRHI* vkRhi, uint32_t imageIdx) {
    SVK_RHI_GPU_ZONE_C(gpuFusedZone, vkRhi, "Fused Pass (Forward + PostProcess)", tracy_color::GpuPass);
    vkRhi->BeginDebugLabel("Render_FusedPass", 1.0f, 0.5f, 0.0f);

    VkClearValue cl[3] = {};
    cl[0].color = {{0.05f, 0.05f, 0.2f, 1.0f}};
    cl[1].depthStencil = {1.0f, 0};
    cl[2].color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = engine->renderPass;
    rp.framebuffer = engine->swapchainMgr.swapchainFramebuffers[imageIdx];
    rp.renderArea.extent = engine->swapchainMgr.swapchainExtent;
    rp.clearValueCount = 3;
    rp.pClearValues = cl;

    if (core.render.billboardMode) {
        vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
        VulkanCommandList cmdListWrapper(vkRhi, cb);
        cmdListWrapper.SetViewport(0.0f, 0.0f, static_cast<float>(engine->swapchainMgr.swapchainExtent.width),
                                   static_cast<float>(engine->swapchainMgr.swapchainExtent.height), 0.0f, 1.0f);
        cmdListWrapper.SetScissor(0, 0, engine->swapchainMgr.swapchainExtent.width, engine->swapchainMgr.swapchainExtent.height);
        vkRhi->BindGlobalDescriptor(&cmdListWrapper);

        if (core.render.showEnvmap) {
            cmdListWrapper.BindPipeline(vkRhi->GetPipeline(PipelineType::Skybox));
            cmdListWrapper.Draw(3, 1, 0, 0);
        }

        const glm::vec3 camPos = core.camera.position;
        BillboardSoA* soa = &core.scene.billboardSoA;

        struct alignas(8) BillboardSortItem {
            uint32_t distBits;
            uint32_t index;
        };

        BillboardSortItem* sortItems = static_cast<BillboardSortItem*>(arena_alloc(&tls_scratch.arena, soa->count * sizeof(BillboardSortItem), 8));
        const glm::vec4* __restrict posArray = static_cast<const glm::vec4*>(__builtin_assume_aligned(soa->pos, 64));
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC unroll 4
#endif
        for (int i = 0; i < soa->count; ++i) {
            __builtin_prefetch(&posArray[i + 8], 0, 1);
            float dx = posArray[i].x - camPos.x;
            float dy = posArray[i].y - camPos.y;
            float dz = posArray[i].z - camPos.z;
            float distSq = (dx * dx) + (dy * dy) + (dz * dz);
            std::memcpy(&sortItems[i].distBits, &distSq, sizeof(uint32_t));
            sortItems[i].index = static_cast<uint32_t>(i);
        }

        std::sort(sortItems, sortItems + soa->count, [](const BillboardSortItem& a, const BillboardSortItem& b) { return a.distBits > b.distBits; });

        uint32_t* tempIndices = static_cast<uint32_t*>(arena_alloc(&tls_scratch.arena, soa->count * sizeof(uint32_t), 4));
        for (int i = 0; i < soa->count; ++i) {
            tempIndices[i] = sortItems[i].index;
        }

        vkRhi->UpdateBillboardInstances(tempIndices, soa->count);

        cmdListWrapper.BindGraphicsPipeline(engine->billboardPipeline.get());
        vkRhi->BindGlobalDescriptor(&cmdListWrapper);
        vkRhi->BindMeshBuffers(&cmdListWrapper, true);
        cmdListWrapper.Draw(6, static_cast<uint32_t>(soa->count), 0, 0);

        if (core.render.wireframeMode) {
            constexpr size_t kUboStride = (sizeof(UBOData) + 255) & ~255;
            uint32_t dynamicOffset = static_cast<uint32_t>((engine->currentFrameIndex % 3) * kUboStride);

            DebugPushConstant dp = {};
            dp.model = glm::mat4(1.0f);
            dp.radius = 1.0f;

            cmdListWrapper.BindGraphicsPipeline(engine->debugTrianglePipeline.get());
            cmdListWrapper.SetGraphicsBindGroup(0, engine->globalBindGroup.get(), 1, &dynamicOffset);
            dp.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.1f);
            dp.mode = 1;
            dp.stippled = 2;
            cmdListWrapper.PushGraphicsConstants(ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(DebugPushConstant), &dp);
            cmdListWrapper.Draw(6, kGridSize * kGridSize, 0, 0);

            cmdListWrapper.BindGraphicsPipeline(engine->debugLinePipeline.get());
            cmdListWrapper.SetGraphicsBindGroup(0, engine->globalBindGroup.get(), 1, &dynamicOffset);
            dp.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
            dp.mode = 1;
            dp.stippled = 0;
            cmdListWrapper.PushGraphicsConstants(ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(DebugPushConstant), &dp);
            cmdListWrapper.Draw(8, kGridSize * kGridSize, 0, 0);

            dp.color = glm::vec4(1.0f, 1.0f, 0.0f, 0.5f);
            dp.mode = 0;
            dp.stippled = 1;
            cmdListWrapper.PushGraphicsConstants(ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(DebugPushConstant), &dp);
            cmdListWrapper.Draw(24, kGridSize * kGridSize, 0, 0);
        }
    } else {
        if (!engine->secondaryForwardRecorded) {
            for (uint32_t s = 0; s < 3; ++s) {
                record_secondary_forward_pass(engine, core, vkRhi, s);
            }
            engine->secondaryForwardRecorded = true;
        }
        vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
        uint32_t slot = engine->currentFrameIndex % 3;
        vkCmdExecuteCommands(cb, 1, &engine->secondaryForwardCb[slot]);
    }

    vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.width = static_cast<float>(engine->swapchainMgr.swapchainExtent.width);
    vp.height = static_cast<float>(engine->swapchainMgr.swapchainExtent.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cb, 0, 1, &vp);

    VkRect2D sc{};
    sc.extent = engine->swapchainMgr.swapchainExtent;
    vkCmdSetScissor(cb, 0, 1, &sc);

    VulkanCommandList cmd(vkRhi, cb);
    cmd.BindGraphicsPipeline(engine->postProcessPipeline.get());
    cmd.SetGraphicsBindGroup(0, engine->postProcessBindGroup.get());
    cmd.Draw(3, 1, 0, 0);

    vkCmdEndRenderPass(cb);
    vkRhi->EndDebugLabel();
}

static void update_frame_scene(VulkanEngine* engine, EngineState* appState, CoreEngine& core) {
    SVK_TRACY_ZONE_SCOPED_C("Frame Scene Update", tracy_color::CpuUpdate);
    vk_check_ibl_bake_status(engine);
    vk_process_ready_environment_texture(engine);
    core_engine_update(&core, &appState->currentInput, 0.25f);

    engine->bloom.enabled = core.render.bloomEnabled;
    engine->bloom.intensity = core.render.bloomIntensity;
    engine->bloom.threshold = core.render.bloomThreshold;
    engine->bloom.softKnee = core.render.bloomSoftKnee;
    engine->bloom.filterRadius = core.render.bloomFilterRadius;

    TextureHandle activeBloomTex = engine->bloom.GetActiveDebugTexture(core.render.bloomDebugMode, core.render.bloomDebugMip);
    if (activeBloomTex != engine->lastBoundBloomTexture) {
        update_postprocess_bloom_texture(engine, activeBloomTex);
        engine->lastBoundBloomTexture = activeBloomTex;
    }

    engine->autoexposure.enabled = core.render.autoExposureEnabled;
    engine->autoexposure.debugOverlay = core.render.autoExposureDebug;
    engine->autoexposure.speedUp = core.render.autoExposureSpeedUp;
    engine->autoexposure.speedDown = core.render.autoExposureSpeedDown;
    engine->autoexposure.keyValue = core.render.autoExposureKeyValue;
    engine->autoexposure.minPercentile = core.render.autoExposureMinPercentile;
    engine->autoexposure.maxPercentile = core.render.autoExposureMaxPercentile;

    appState->currentInput.mouseDeltaX = 0.0f;
    appState->currentInput.mouseDeltaY = 0.0f;
    appState->currentInput.scrollDelta = 0.0f;
}

static void update_frame_ubo(VulkanEngine* engine, const CoreEngine& core, IRHI* rhi) {
    (void)engine;
    UBOData uboData;
    uboData.modelRotation = glm::mat4(1.0f);
    glm::mat4 view = glm::lookAt(core.camera.position, core.camera.position + core.camera.front, core.camera.up);

    uint32_t renderWidth;
    uint32_t renderHeight;
    rhi->GetResolution(&renderWidth, &renderHeight);
    const float aspect = static_cast<float>(renderWidth) / static_cast<float>(renderHeight);

    glm::mat4 proj = glm::perspective(glm::radians(core.camera.zoom), aspect, kNearPlane, kFarPlane);
    proj[1][1] *= -1;
    uboData.vp = proj * view;

    glm::mat4 skyboxView = glm::mat4(glm::mat3(view));
    glm::mat4 skyboxProj = glm::perspective(glm::radians(kLegacyDefaultFov), aspect, kNearPlane, kFarPlane);
    skyboxProj[1][1] *= -1;
    uboData.invViewProj = glm::inverse(skyboxProj * skyboxView);

    uboData.cameraPosEnvLod = glm::vec4(core.camera.position, core.render.envLod);
    uboData.debugParams = glm::vec4(static_cast<float>(core.render.iblDebugMode), core.render.iblDebugScale, core.render.billboardMode ? 1.0f : 0.0f, 0.0f);
    uboData.postParams1 = glm::vec4(core.render.exposure, core.render.saturation, core.render.contrast, core.render.gamma);
    uboData.postParams2 = glm::vec4(core.render.gain, core.render.offset, core.render.wbTemp, core.render.wbTint);
    uboData.view = view;
    uboData.proj = proj;
    uboData.windowSize = glm::vec4(static_cast<float>(renderWidth), static_cast<float>(renderHeight), 0.0f, 0.0f);

    rhi->UpdateUBO(uboData);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2,fma")))
#endif
static void update_instance_transforms(VulkanEngine* engine, const CoreEngine& core) {
    if (engine->transformBufferMapped && core.scene.instancePositions) {
        glm::vec4* __restrict transforms = static_cast<glm::vec4*>(__builtin_assume_aligned(engine->transformBufferMapped, 16));
        const glm::vec3* __restrict positions = static_cast<const glm::vec3*>(__builtin_assume_aligned(core.scene.instancePositions, 64));
        const uint32_t count = core.scene.instanceCount;

        uint32_t i = 0;
        for (; i + 4 <= count; i += 4) {
            __builtin_prefetch(&positions[i + 16], 0, 1);
            transforms[i + 0] = glm::vec4(positions[i + 0], 1.0f);
            transforms[i + 1] = glm::vec4(positions[i + 1], 1.0f);
            transforms[i + 2] = glm::vec4(positions[i + 2], 1.0f);
            transforms[i + 3] = glm::vec4(positions[i + 3], 1.0f);
        }
        for (; i < count; ++i) {
            transforms[i] = glm::vec4(positions[i], 1.0f);
        }
    }
}

static void record_postprocess_pass(VkCommandBuffer cb, VulkanEngine* engine, const CoreEngine& core, VulkanRHI* vkRhi) {
    SVK_TRACY_VK_ZONE_C(gpuPostProcess, engine, cb, "PostProcess Pass", tracy_color::GpuPassPostProcess);
    vkRhi->BeginDebugLabel("Render_PostProcess", 0.8f, 0.1f, 0.8f);

    uint32_t imageIdx = engine->lastRenderedImageIndex;
    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = engine->renderPassLoad;
    rp.framebuffer = engine->swapchainMgr.swapchainFramebuffers[imageIdx];
    rp.renderArea.extent = engine->swapchainMgr.swapchainExtent;
    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.width = static_cast<float>(engine->swapchainMgr.swapchainExtent.width);
    vp.height = static_cast<float>(engine->swapchainMgr.swapchainExtent.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cb, 0, 1, &vp);

    VkRect2D sc{};
    sc.extent = engine->swapchainMgr.swapchainExtent;
    vkCmdSetScissor(cb, 0, 1, &sc);

    VulkanCommandList cmd(vkRhi, cb);
    cmd.BindGraphicsPipeline(engine->postProcessPipeline.get());
    cmd.SetGraphicsBindGroup(0, engine->postProcessBindGroup.get());

    struct PostProcessPushConstants {
        float bloomIntensity;
        int32_t bloomEnabled;
        int32_t bloomDebugMode;
        int32_t bloomDebugMip;
        float exposure;
        float saturation;
        float contrast;
        float gamma;
        float gain;
        float offset;
        float wbTemp;
        float wbTint;
        int32_t autoExposureEnabled;
        int32_t autoExposureDebug;
        float screenWidth;
        float screenHeight;
    } ppPc{engine->bloom.intensity,
           engine->bloom.enabled ? 1 : 0,
           core.render.bloomDebugMode,
           core.render.bloomDebugMip,
           core.render.exposure,
           core.render.saturation,
           core.render.contrast,
           core.render.gamma,
           core.render.gain,
           core.render.offset,
           core.render.wbTemp,
           core.render.wbTint,
           core.render.autoExposureEnabled ? 1 : 0,
           core.render.autoExposureDebug ? 1 : 0,
           static_cast<float>(engine->swapchainMgr.swapchainExtent.width),
           static_cast<float>(engine->swapchainMgr.swapchainExtent.height)};
    cmd.PushGraphicsConstants(ShaderStage::Fragment, 0, sizeof(PostProcessPushConstants), &ppPc);

    cmd.Draw(3, 1, 0, 0);

    vkCmdEndRenderPass(cb);
    vkRhi->EndDebugLabel();
}

static void build_render_graph(rhi::RenderGraph& graph, VulkanEngine* engine, VulkanRHI* vkRhi, VkExtent2D currentExtent, bool runFused, bool currentDebugMode,
                               bool isBloomActive, bool isAutoExposureActive, rhi::ResourceHandle vSwapchain, rhi::ResourceHandle vColor,
                               rhi::ResourceHandle vDepth, rhi::ResourceHandle vBloom, rhi::ResourceHandle vExposure) {
    // Dummy transient resources to test Memory Aliasing (Phase 4)
    rhi::TransientImageDesc gbufferDesc = {currentExtent.width, currentExtent.height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                           VK_IMAGE_ASPECT_COLOR_BIT};
    auto vGBuffer1 = graph.CreateTransientImage("GBuffer_Albedo", gbufferDesc);
    auto vGBuffer2 = graph.CreateTransientImage("GBuffer_Normal", gbufferDesc);
    auto vSSAO = graph.CreateTransientImage("SSAO_Mask", gbufferDesc);

    // Dummy GBuffer Pass (Writes to GBuffer)
    graph.AddPass("GBufferPass", {}, {{vGBuffer1, rhi::ResourceState::RenderTarget}, {vGBuffer2, rhi::ResourceState::RenderTarget}},
                  [](VkCommandBuffer) { /* Dummy execute */ });

    // Dummy SSAO Pass (Reads GBuffer, Writes SSAO)
    graph.AddPass("SSAOPass", {{vGBuffer2, rhi::ResourceState::ShaderRead}}, {{vSSAO, rhi::ResourceState::RenderTarget}},
                  [](VkCommandBuffer) { /* Dummy execute */ });

    if (runFused) {
        graph.AddPass("FusedForwardPostProcessPass", {{vGBuffer1, rhi::ResourceState::ShaderRead}, {vSSAO, rhi::ResourceState::ShaderRead}},
                      {{vSwapchain, rhi::ResourceState::RenderTarget}},
                      [engine, vkRhi](VkCommandBuffer cb) { record_fused_pass(cb, engine, engine->appState->core, vkRhi, engine->lastRenderedImageIndex); });
    } else {
        // ForwardPass reads SSAO and GBuffer1
        graph.AddPass("ForwardPass", {{vGBuffer1, rhi::ResourceState::ShaderRead}, {vSSAO, rhi::ResourceState::ShaderRead}},
                      {{vColor, rhi::ResourceState::RenderTarget}, {vDepth, rhi::ResourceState::RenderTarget}},
                      [engine, vkRhi](VkCommandBuffer) { record_forward_pass(vkRhi->GetMainCommandList(), engine, engine->appState->core, vkRhi); });

        // AutoExposure Compute Pass (Reads Color HDR, Writes Exposure Texture)
        if (isAutoExposureActive) {
            graph.AddPass("AutoExposureComputePass", {{vColor, rhi::ResourceState::ShaderRead}}, {{vExposure, rhi::ResourceState::ComputeWrite}},
                          [engine, currentExtent](VkCommandBuffer cb) {
                              float dt = engine->appState->core.time.lastFrameDeltaSeconds;
                              engine->autoexposure.RecordPasses(engine, cb, engine->swapchainMgr.colorAttachment.get(), currentExtent.width,
                                                                currentExtent.height, dt);
                          });
        }

        // Bloom Compute Pass (Reads Color HDR, Writes Bloom Mip 0)
        if (isBloomActive) {
            graph.AddPass("BloomComputePass", {{vColor, rhi::ResourceState::ShaderRead}}, {{vBloom, rhi::ResourceState::ComputeWrite}},
                          [engine](VkCommandBuffer cb) { engine->bloom.RecordPasses(engine, cb); });
        }

        if (currentDebugMode) {
            graph.AddPass("DebugPass", {{vColor, rhi::ResourceState::RenderTarget}},
                          {{vColor, rhi::ResourceState::RenderTarget}, {vDepth, rhi::ResourceState::RenderTarget}},
                          [engine, vkRhi](VkCommandBuffer) { record_debug_pass(vkRhi->GetMainCommandList(), engine, engine->appState->core, vkRhi); });
        }

        std::vector<rhi::PassDependency> postProcessInputs = {{vColor, rhi::ResourceState::ShaderRead}};
        if (isBloomActive) {
            postProcessInputs.push_back({vBloom, rhi::ResourceState::ShaderRead});
        }
        if (isAutoExposureActive) {
            postProcessInputs.push_back({vExposure, rhi::ResourceState::ShaderRead});
        }

        graph.AddPass("PostProcessPass", postProcessInputs, {{vSwapchain, rhi::ResourceState::RenderTarget}},
                      [engine, vkRhi](VkCommandBuffer cb) { record_postprocess_pass(cb, engine, engine->appState->core, vkRhi); });
    }

    graph.Compile();
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2,fma")))
#endif
GfxResult vk_draw_frame_internal(VulkanEngine* engine, RecreateSwapchainFn recreateSwapchain) {
    SVK_TRACY_ZONE_SCOPED_C("Total Frame", tracy_color::FrameTotal);

    // Reset the TLS Scratch Arena for this frame
    arena_reset(&tls_scratch.arena);

    // L1 Cache Optimization: Bring hot pointers/references to stack
    EngineState* appState = engine->appState;
    IRHI* rhi = appState->rhi;
    CoreEngine& core = appState->core;

    uint32_t idx;
    {
        SVK_TRACY_ZONE_SCOPED_C("Frame Acquire Swapchain", tracy_color::CpuAcquire);
        SwapchainStatus status = rhi->AcquireNextImage(&idx);
        if (status == SwapchainStatus::NeedRecreate) {
            return recreateSwapchain(engine);
        }
        if (status == SwapchainStatus::Error) {
            return GfxResult::ErrorInitializationFailed;
        }
    }
    engine->lastRenderedImageIndex = idx;
    emit_tracy_frame_capture(engine);

    update_frame_scene(engine, appState, core);
    update_frame_ubo(engine, core, rhi);
    update_instance_transforms(engine, core);

    {
        SVK_TRACY_ZONE_SCOPED_C("RenderGraph Execute & Record", tracy_color::CpuRecord);
        if (rhi->BeginFrame() != RHIResult::Success) {
            return GfxResult::ErrorInitializationFailed;
        }

        IRenderCommandList* cmdList = rhi->GetMainCommandList();

        rhi->CollectProfiling();

        {
            // PHASE 2: SCISSION DES PASSES (Strangler Fig - True Zero-Allocation Render Loop)
            static thread_local rhi::RenderGraph graph;
            static thread_local bool graphCompiled = false;
            static thread_local rhi::ResourceHandle vSwapchain = 0;
            static thread_local rhi::ResourceHandle vColor = 0;
            static thread_local rhi::ResourceHandle vDepth = 0;
            static thread_local rhi::ResourceHandle vBloom = 0;
            static thread_local rhi::ResourceHandle vExposure = 0;
            static thread_local bool lastDebugMode = false;
            static thread_local bool lastWireframe = false;
            static thread_local bool lastShowEnvmap = true;
            static thread_local VkExtent2D lastExtent = {0, 0};

            static thread_local bool lastSubpassFusion = true;
            static thread_local bool lastBloomActive = false;
            static thread_local bool lastAutoExposureActive = false;

            if (lastWireframe != core.render.wireframeMode || lastShowEnvmap != core.render.showEnvmap) {
                engine->secondaryForwardRecorded = false;
                lastWireframe = core.render.wireframeMode;
                lastShowEnvmap = core.render.showEnvmap;
            }

            bool currentDebugMode = (core.render.wireframeMode && core.render.billboardMode);
            VkExtent2D currentExtent = engine->swapchainMgr.swapchainExtent;
            VulkanRHI* vkRhi = static_cast<VulkanRHI*>(rhi);
            VulkanCommandList* vkCmdList = static_cast<VulkanCommandList*>(cmdList);

            engine->lastRenderedImageIndex = idx;

            bool isBloomActive = (core.render.bloomEnabled || core.render.bloomDebugMode != 0);
            bool isAutoExposureActive = (core.render.autoExposureEnabled || core.render.autoExposureDebug);
            bool runFused = (engine->useSubpassFusion && !isBloomActive && !isAutoExposureActive);

            if (!graphCompiled || lastDebugMode != currentDebugMode || lastExtent.width != currentExtent.width || lastExtent.height != currentExtent.height ||
                lastSubpassFusion != runFused || lastBloomActive != isBloomActive || lastAutoExposureActive != isAutoExposureActive) {
                engine->secondaryForwardRecorded = false;
                lastExtent = currentExtent;
                lastDebugMode = currentDebugMode;
                lastSubpassFusion = runFused;
                lastBloomActive = isBloomActive;
                lastAutoExposureActive = isAutoExposureActive;
                graph.Reset();

                vSwapchain = graph.CreateVirtualImage("Swapchain");
                vColor = graph.CreateVirtualImage("Color");
                vDepth = graph.CreateVirtualImage("Depth");
                vBloom = graph.CreateVirtualImage("Bloom_Blur");
                vExposure = graph.CreateVirtualImage("AutoExposure_Texture");

                build_render_graph(graph, engine, vkRhi, currentExtent, runFused, currentDebugMode, isBloomActive, isAutoExposureActive, vSwapchain, vColor,
                                   vDepth, vBloom, vExposure);
                graphCompiled = true;
            }

            graph.BindPhysicalResource(vSwapchain, engine->swapchainMgr.swapchainImages[idx], engine->swapchainMgr.swapchainImageFormat,
                                       VK_IMAGE_ASPECT_COLOR_BIT);
            graph.BindPhysicalResource(vColor, vkRhi->GetVkImage(engine->swapchainMgr.colorAttachment.get()), VK_FORMAT_R16G16B16A16_SFLOAT,
                                       VK_IMAGE_ASPECT_COLOR_BIT);
            graph.BindPhysicalResource(vDepth, vkRhi->GetVkImage(engine->swapchainMgr.depthImage.get()), engine->swapchainMgr.depthFormat,
                                       VK_IMAGE_ASPECT_DEPTH_BIT);
            if (engine->bloom.upMips[0].texture.is_valid()) {
                graph.BindPhysicalResource(vBloom, vkRhi->GetVkImage(engine->bloom.upMips[0].texture.get()), VK_FORMAT_R16G16B16A16_SFLOAT,
                                           VK_IMAGE_ASPECT_COLOR_BIT);
            }
            if (engine->autoexposure.exposureTexture.is_valid()) {
                graph.BindPhysicalResource(vExposure, vkRhi->GetVkImage(engine->autoexposure.exposureTexture.get()), VK_FORMAT_R32G32B32A32_SFLOAT,
                                           VK_IMAGE_ASPECT_COLOR_BIT);
            }

            graph.Execute(vkCmdList->GetVkCommandBuffer());

            record_tracy_frame_capture(engine, vkCmdList->GetVkCommandBuffer(), engine->swapchainMgr.swapchainImages[idx], engine->swapchainMgr.swapchainExtent,
                                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }

        rhi->EndFrame();
    }

    {
        SVK_TRACY_ZONE_SCOPED_C("Frame Queue Submit & Present", tracy_color::CpuPresent);
        SwapchainStatus status = rhi->SubmitAndPresent(idx);
        if (status == SwapchainStatus::NeedRecreate) {
            return recreateSwapchain(engine);
        }
        const GfxResult statusMap[3] = {GfxResult::Success, GfxResult::ErrorInitializationFailed, GfxResult::ErrorInitializationFailed};
        engine->totalFramesRendered++;
        return statusMap[static_cast<uint8_t>(status)];
    }
}
