#include "vk_engine_frame.h"
#include "rhi/command_list.h"

#include "runtime_controls.h"
#include "tracy_client.h"
#include "tracy_vulkan.h"
#include "vk_engine_envmap.h"
#include "vk_engine_runtime.h"

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

bool vk_draw_frame_internal(VulkanEngine* engine, RecreateSwapchainFn recreateSwapchain) {
    SVK_TRACY_ZONE_SCOPED("vk_draw_frame_internal");

    // L1 Cache Optimization: Bring hot pointers/references to stack
    EngineState* appState = engine->appState;
    IRHI* rhi = appState->rhi;
    CoreEngine& core = appState->core;

    uint32_t idx;
    {
        SVK_TRACY_ZONE_SCOPED("Frame CPU Acquire");
        SwapchainStatus status = rhi->AcquireNextImage(&idx);
        if (status == SwapchainStatus::NeedRecreate) {
            return recreateSwapchain(engine);
        }
        if (status == SwapchainStatus::Error) {
            return false;
        }
    }
    engine->lastRenderedImageIndex = idx;

    {
        SVK_TRACY_ZONE_SCOPED("Frame CPU Update");
        vk_process_ready_environment_texture(engine);
        // Inputs are now handled by HandleInputs() before DrawFrame()
        core_engine_update(&core, &appState->currentInput, 0.25f);

        appState->currentInput.mouseDeltaX = 0.0f;
        appState->currentInput.mouseDeltaY = 0.0f;
        appState->currentInput.scrollDelta = 0.0f;
    }

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

    if (engine->transformBufferMapped && core.scene.instancePositions) {
        glm::mat4* transforms = static_cast<glm::mat4*>(engine->transformBufferMapped);
        const glm::vec3* __restrict positions = static_cast<const glm::vec3*>(__builtin_assume_aligned(core.scene.instancePositions, 64));
        const glm::mat4 baseModelRot = uboData.modelRotation;
        const uint32_t count = core.scene.instanceCount;

        for (uint32_t i = 0; i < count; ++i) {
            __builtin_prefetch(&positions[i + 8], 0, 1);
            transforms[i] = glm::translate(glm::mat4(1.0f), positions[i]) * baseModelRot;
        }
    }

    rhi->UpdateUBO(uboData);

    {
        SVK_TRACY_ZONE_SCOPED("Frame CPU Record");
        if (!rhi->BeginFrame()) {
            return false;
        }

        IRenderCommandList* cmdList = rhi->GetMainCommandList();

        rhi->CollectProfiling();

        {
            SVK_RHI_GPU_ZONE(gpuFrameZone, rhi, "GPU Frame");

            rhi->BeginDebugLabel("Render_Frame_Graphics", 1.0f, 0.5f, 0.0f);

            rhi->BeginDebugLabel("RenderPass_Begin_And_Bindings", 1.0f, 0.8f, 0.2f);
            cmdList->BeginRenderPass();
            rhi->BindGlobalDescriptor(cmdList);
            rhi->EndDebugLabel();

            {
                SVK_RHI_GPU_ZONE(gpuSkyboxZone, rhi, "GPU Skybox");
                rhi->BeginDebugLabel("Render_Skybox_EnvMap", 0.2f, 0.5f, 1.0f);
                if (core.render.showEnvmap) {
                    cmdList->BindPipeline(rhi->GetPipeline(PipelineType::Skybox));
                    cmdList->Draw(3, 1, 0, 0);
                }
                rhi->EndDebugLabel();
            }

            {
                SVK_RHI_GPU_ZONE(gpuSphereZone, rhi, "GPU Spheres");
                rhi->BeginDebugLabel("Render_Spheres_Instanced", 0.0f, 1.0f, 0.4f);
                if (core.render.billboardMode) {
                    const glm::vec3 camPos = core.camera.position;
                    BillboardSoA* soa = &core.scene.billboardSoA;

                    std::size_t savedOffset = core.scene.arena.offset;

                    struct alignas(8) BillboardSortItem {
                        uint32_t distBits;
                        uint32_t index;
                    };

                    BillboardSortItem* sortItems = static_cast<BillboardSortItem*>(arena_alloc(&core.scene.arena, soa->count * sizeof(BillboardSortItem), 8));

                    // 1. Linear Gather: Calculate distances and pack indices
                    const glm::vec4* __restrict posArray = static_cast<const glm::vec4*>(__builtin_assume_aligned(soa->pos, 64));
                    for (int i = 0; i < soa->count; ++i) {
                        __builtin_prefetch(&posArray[i + 8], 0, 1);
                        glm::vec3 da = glm::vec3(posArray[i]) - camPos;
                        float distSq = glm::dot(da, da);
                        std::memcpy(&sortItems[i].distBits, &distSq, sizeof(uint32_t));
                        sortItems[i].index = static_cast<uint32_t>(i);
                    }

                    // 2. Cache-Friendly In-Place Sort: Swap adjacent items
                    std::sort(sortItems, sortItems + soa->count, [](const BillboardSortItem& a, const BillboardSortItem& b) {
                        return a.distBits > b.distBits; // Positive floats sort correctly as uint32_t
                    });

                    // 3. Linear Scatter: Extract payload for Vulkan
                    uint32_t* tempIndices = static_cast<uint32_t*>(arena_alloc(&core.scene.arena, soa->count * sizeof(uint32_t), 4));
                    for (int i = 0; i < soa->count; ++i) {
                        tempIndices[i] = sortItems[i].index;
                    }

                    rhi->UpdateBillboardInstances(tempIndices, soa->count);

                    core.scene.arena.offset = savedOffset;

                    cmdList->BindPipeline(rhi->GetPipeline(PipelineType::Billboard));
                    rhi->BindMeshBuffers(cmdList, true);
                    cmdList->Draw(6, static_cast<uint32_t>(soa->count), 0, 0);
                } else {
                    cmdList->BindPipeline(rhi->GetPipeline(core.render.wireframeMode ? PipelineType::Wireframe : PipelineType::Graphics));
                    rhi->BindMeshBuffers(cmdList, false);
                    cmdList->DrawIndexed(engine->indexCount, kGridSize * kGridSize, 0, 0, 0);
                }

                if (core.render.wireframeMode && core.render.billboardMode) {
                    DebugPushConstant dp = {};
                    dp.model = glm::mat4(1.0f);
                    dp.radius = 1.0f;

                    cmdList->BindPipeline(rhi->GetPipeline(PipelineType::DebugTriangle));
                    dp.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.1f);
                    dp.mode = 1;
                    dp.stippled = 2;
                    cmdList->PushConstants(engine->debugPipelineLayout.get(), ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(DebugPushConstant), &dp);
                    cmdList->Draw(6, kGridSize * kGridSize, 0, 0);

                    cmdList->BindPipeline(rhi->GetPipeline(PipelineType::DebugLine));
                    dp.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
                    dp.mode = 1;
                    dp.stippled = 0;
                    cmdList->PushConstants(engine->debugPipelineLayout.get(), ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(DebugPushConstant), &dp);
                    cmdList->Draw(8, kGridSize * kGridSize, 0, 0);

                    dp.color = glm::vec4(1.0f, 1.0f, 0.0f, 0.5f);
                    dp.mode = 0;
                    dp.stippled = 1;
                    cmdList->PushConstants(engine->debugPipelineLayout.get(), ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(DebugPushConstant), &dp);
                    cmdList->Draw(24, kGridSize * kGridSize, 0, 0);
                }
                rhi->EndDebugLabel();
            }

            cmdList->EndRenderPass();
            rhi->EndDebugLabel();
        }

        rhi->EndFrame();
    }

    {
        SVK_TRACY_ZONE_SCOPED("Frame CPU Submit and Present");
        SwapchainStatus status = rhi->SubmitAndPresent(idx);
        if (status == SwapchainStatus::NeedRecreate) {
            return recreateSwapchain(engine);
        }
        return status == SwapchainStatus::Ok;
    }
}
