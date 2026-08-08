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

    uint32_t idx;
    {
        SVK_TRACY_ZONE_SCOPED("Frame CPU Acquire");
        SwapchainStatus status = engine->appState->rhi->AcquireNextImage(&idx);
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
        core_engine_update(&engine->appState->core, &engine->appState->currentInput, 0.25f);

        engine->appState->currentInput.mouseDeltaX = 0.0f;
        engine->appState->currentInput.mouseDeltaY = 0.0f;
        engine->appState->currentInput.scrollDelta = 0.0f;
    }

    UBOData uboData;
    uboData.modelRotation = glm::mat4(1.0f);
    glm::mat4 view = glm::lookAt(engine->appState->core.camera.position, engine->appState->core.camera.position + engine->appState->core.camera.front,
                                 engine->appState->core.camera.up);

    uint32_t renderWidth;
    uint32_t renderHeight;
    engine->appState->rhi->GetResolution(&renderWidth, &renderHeight);
    const float aspect = static_cast<float>(renderWidth) / static_cast<float>(renderHeight);

    glm::mat4 proj = glm::perspective(glm::radians(engine->appState->core.camera.zoom), aspect, kNearPlane, kFarPlane);
    proj[1][1] *= -1;
    uboData.vp = proj * view;

    glm::mat4 skyboxView = glm::mat4(glm::mat3(view));
    glm::mat4 skyboxProj = glm::perspective(glm::radians(kLegacyDefaultFov), aspect, kNearPlane, kFarPlane);
    skyboxProj[1][1] *= -1;
    uboData.invViewProj = glm::inverse(skyboxProj * skyboxView);

    uboData.cameraPosEnvLod = glm::vec4(engine->appState->core.camera.position, engine->appState->core.envLod);
    uboData.debugParams = glm::vec4(static_cast<float>(engine->appState->core.iblDebugMode), engine->appState->core.iblDebugScale,
                                    engine->appState->core.billboardMode ? 1.0f : 0.0f, 0.0f);
    uboData.postParams1 =
        glm::vec4(engine->appState->core.exposure, engine->appState->core.saturation, engine->appState->core.contrast, engine->appState->core.gamma);
    uboData.postParams2 = glm::vec4(engine->appState->core.gain, engine->appState->core.offset, engine->appState->core.wbTemp, engine->appState->core.wbTint);
    uboData.view = view;
    uboData.proj = proj;

    uboData.windowSize = glm::vec4(static_cast<float>(renderWidth), static_cast<float>(renderHeight), 0.0f, 0.0f);

    engine->appState->rhi->UpdateUBO(uboData);

    {
        SVK_TRACY_ZONE_SCOPED("Frame CPU Record");
        if (!engine->appState->rhi->BeginFrame()) {
            return false;
        }

        engine->appState->rhi->CollectProfiling();

        {
            SVK_RHI_GPU_ZONE(gpuFrameZone, engine->appState->rhi, "GPU Frame");

            engine->appState->rhi->BeginDebugLabel("Render_Frame_Graphics", 1.0f, 0.5f, 0.0f);

            engine->appState->rhi->BeginDebugLabel("RenderPass_Begin_And_Bindings", 1.0f, 0.8f, 0.2f);
            engine->appState->rhi->BeginRenderPass();
            engine->appState->rhi->BindGlobalDescriptor();
            engine->appState->rhi->EndDebugLabel();

            {
                SVK_RHI_GPU_ZONE(gpuSkyboxZone, engine->appState->rhi, "GPU Skybox");
                engine->appState->rhi->BeginDebugLabel("Render_Skybox_EnvMap", 0.2f, 0.5f, 1.0f);
                if (engine->appState->core.showEnvmap) {
                    engine->appState->rhi->BindPipeline(PipelineType::Skybox);
                    engine->appState->rhi->Draw(3, 1);
                }
                engine->appState->rhi->EndDebugLabel();
            }

            {
                SVK_RHI_GPU_ZONE(gpuSphereZone, engine->appState->rhi, "GPU Spheres");
                engine->appState->rhi->BeginDebugLabel("Render_Spheres_Instanced", 0.0f, 1.0f, 0.4f);
                if (engine->appState->core.billboardMode) {
                    const glm::vec3 camPos = engine->appState->core.camera.position;
                    BillboardSoA* soa = &engine->appState->core.billboardSoA;

                    std::size_t savedOffset = engine->appState->core.arena.offset;

                    uint64_t* sortKeys = static_cast<uint64_t*>(arena_alloc(&engine->appState->core.arena, soa->count * sizeof(uint64_t), 8));
                    for (int i = 0; i < soa->count; ++i) {
                        glm::vec3 da = soa->pos[i] - camPos;
                        float distSq = glm::dot(da, da);
                        uint32_t distBits;
                        std::memcpy(&distBits, &distSq, sizeof(uint32_t));
                        sortKeys[i] = (static_cast<uint64_t>(distBits) << 32) | static_cast<uint32_t>(i);
                    }

                    // Sort descending (furthest first)
                    std::sort(sortKeys, sortKeys + soa->count, std::greater<uint64_t>());

                    BillboardInstance* tempInstances =
                        static_cast<BillboardInstance*>(arena_alloc(&engine->appState->core.arena, soa->count * sizeof(BillboardInstance), 16));
                    for (int i = 0; i < soa->count; ++i) {
                        int srcIdx = static_cast<int>(sortKeys[i] & 0xFFFFFFFF);
                        tempInstances[i].pos = soa->pos[srcIdx];
                        tempInstances[i].materialIdx = soa->materialIdx[srcIdx];
                    }

                    engine->appState->rhi->UpdateBillboardInstances(tempInstances, soa->count);

                    engine->appState->core.arena.offset = savedOffset;

                    engine->appState->rhi->BindPipeline(PipelineType::Billboard);
                    engine->appState->rhi->BindMeshBuffers(true);
                    engine->appState->rhi->Draw(6, static_cast<uint32_t>(soa->count));
                } else {
                    engine->appState->rhi->BindPipeline(engine->appState->core.wireframeMode ? PipelineType::Wireframe : PipelineType::Graphics);
                    engine->appState->rhi->BindMeshBuffers(false);
                    engine->appState->rhi->DrawIndexed(engine->indexCount, kGridSize * kGridSize);
                }

                if (engine->appState->core.wireframeMode && engine->appState->core.billboardMode) {
                    DebugPushConstant dp = {};
                    dp.model = glm::mat4(1.0f);
                    dp.radius = 1.0f;

                    engine->appState->rhi->BindPipeline(PipelineType::DebugTriangle);
                    dp.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.1f);
                    dp.mode = 1;
                    dp.stippled = 2;
                    engine->appState->rhi->PushDebugConstants(&dp, sizeof(DebugPushConstant));
                    engine->appState->rhi->Draw(6, kGridSize * kGridSize);

                    engine->appState->rhi->BindPipeline(PipelineType::DebugLine);
                    dp.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
                    dp.mode = 1;
                    dp.stippled = 0;
                    engine->appState->rhi->PushDebugConstants(&dp, sizeof(DebugPushConstant));
                    engine->appState->rhi->Draw(8, kGridSize * kGridSize);

                    dp.color = glm::vec4(1.0f, 1.0f, 0.0f, 0.5f);
                    dp.mode = 0;
                    dp.stippled = 1;
                    engine->appState->rhi->PushDebugConstants(&dp, sizeof(DebugPushConstant));
                    engine->appState->rhi->Draw(24, kGridSize * kGridSize);
                }
                engine->appState->rhi->EndDebugLabel();
            }

            engine->appState->rhi->EndRenderPass();
            engine->appState->rhi->EndDebugLabel();
        }

        engine->appState->rhi->EndFrame();
    }

    {
        SVK_TRACY_ZONE_SCOPED("Frame CPU Submit and Present");
        SwapchainStatus status = engine->appState->rhi->SubmitAndPresent(idx);
        if (status == SwapchainStatus::NeedRecreate) {
            return recreateSwapchain(engine);
        }
        return status == SwapchainStatus::Ok;
    }
}
