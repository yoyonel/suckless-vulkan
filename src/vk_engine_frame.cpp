#include "vk_engine_frame.h"

#include "runtime_controls.h"
#include "tracy_client.h"
#include "tracy_vulkan.h"
#include "vk_engine_envmap.h"
#include "vk_engine_runtime.h"

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

    uboData.cameraPosEnvLod = glm::vec4(core.camera.position, core.envLod);
    uboData.debugParams = glm::vec4(static_cast<float>(core.iblDebugMode), core.iblDebugScale, core.billboardMode ? 1.0f : 0.0f, 0.0f);
    uboData.postParams1 = glm::vec4(core.exposure, core.saturation, core.contrast, core.gamma);
    uboData.postParams2 = glm::vec4(core.gain, core.offset, core.wbTemp, core.wbTint);
    uboData.view = view;
    uboData.proj = proj;

    uboData.windowSize = glm::vec4(static_cast<float>(renderWidth), static_cast<float>(renderHeight), 0.0f, 0.0f);

    rhi->UpdateUBO(uboData);

    {
        SVK_TRACY_ZONE_SCOPED("Frame CPU Record");
        if (!rhi->BeginFrame()) {
            return false;
        }

        rhi->CollectProfiling();

        {
            SVK_RHI_GPU_ZONE(gpuFrameZone, rhi, "GPU Frame");

            rhi->BeginDebugLabel("Render_Frame_Graphics", 1.0f, 0.5f, 0.0f);

            rhi->BeginDebugLabel("RenderPass_Begin_And_Bindings", 1.0f, 0.8f, 0.2f);
            rhi->BeginRenderPass();
            rhi->BindGlobalDescriptor();
            rhi->EndDebugLabel();

            {
                SVK_RHI_GPU_ZONE(gpuSkyboxZone, rhi, "GPU Skybox");
                rhi->BeginDebugLabel("Render_Skybox_EnvMap", 0.2f, 0.5f, 1.0f);
                if (core.showEnvmap) {
                    rhi->BindPipeline(PipelineType::Skybox);
                    rhi->Draw(3, 1);
                }
                rhi->EndDebugLabel();
            }

            {
                SVK_RHI_GPU_ZONE(gpuSphereZone, rhi, "GPU Spheres");
                rhi->BeginDebugLabel("Render_Spheres_Instanced", 0.0f, 1.0f, 0.4f);
                if (core.billboardMode) {
                    const glm::vec3 camPos = core.camera.position;
                    BillboardSoA* soa = &core.billboardSoA;

                    std::size_t savedOffset = core.arena.offset;

                    struct alignas(16) BillboardSortItem {
                        uint32_t distBits;
                        BillboardInstance inst;
                    };

                    BillboardSortItem* sortItems = static_cast<BillboardSortItem*>(arena_alloc(&core.arena, soa->count * sizeof(BillboardSortItem), 16));

                    // 1. Linear Gather: Calculate distances and pack data into contiguous AoS array
                    for (int i = 0; i < soa->count; ++i) {
                        glm::vec3 da = soa->pos[i] - camPos;
                        float distSq = glm::dot(da, da);
                        std::memcpy(&sortItems[i].distBits, &distSq, sizeof(uint32_t));
                        sortItems[i].inst.pos = soa->pos[i];
                        sortItems[i].inst.materialIdx = soa->materialIdx[i];
                    }

                    // 2. Cache-Friendly In-Place Sort: Swap adjacent items (no random pointer chasing)
                    std::sort(sortItems, sortItems + soa->count, [](const BillboardSortItem& a, const BillboardSortItem& b) {
                        return a.distBits > b.distBits; // Positive floats sort correctly as uint32_t
                    });

                    // 3. Linear Scatter: Extract payload for Vulkan
                    BillboardInstance* tempInstances = static_cast<BillboardInstance*>(arena_alloc(&core.arena, soa->count * sizeof(BillboardInstance), 16));
                    for (int i = 0; i < soa->count; ++i) {
                        tempInstances[i] = sortItems[i].inst;
                    }

                    rhi->UpdateBillboardInstances(tempInstances, soa->count);

                    core.arena.offset = savedOffset;

                    rhi->BindPipeline(PipelineType::Billboard);
                    rhi->BindMeshBuffers(true);
                    rhi->Draw(6, static_cast<uint32_t>(soa->count));
                } else {
                    rhi->BindPipeline(core.wireframeMode ? PipelineType::Wireframe : PipelineType::Graphics);
                    rhi->BindMeshBuffers(false);
                    rhi->DrawIndexed(engine->indexCount, kGridSize * kGridSize);
                }

                if (core.wireframeMode && core.billboardMode) {
                    DebugPushConstant dp = {};
                    dp.model = glm::mat4(1.0f);
                    dp.radius = 1.0f;

                    rhi->BindPipeline(PipelineType::DebugTriangle);
                    dp.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.1f);
                    dp.mode = 1;
                    dp.stippled = 2;
                    rhi->PushDebugConstants(&dp, sizeof(DebugPushConstant));
                    rhi->Draw(6, kGridSize * kGridSize);

                    rhi->BindPipeline(PipelineType::DebugLine);
                    dp.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
                    dp.mode = 1;
                    dp.stippled = 0;
                    rhi->PushDebugConstants(&dp, sizeof(DebugPushConstant));
                    rhi->Draw(8, kGridSize * kGridSize);

                    dp.color = glm::vec4(1.0f, 1.0f, 0.0f, 0.5f);
                    dp.mode = 0;
                    dp.stippled = 1;
                    rhi->PushDebugConstants(&dp, sizeof(DebugPushConstant));
                    rhi->Draw(24, kGridSize * kGridSize);
                }
                rhi->EndDebugLabel();
            }

            rhi->EndRenderPass();
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
