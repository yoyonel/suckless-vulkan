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
        SwapchainStatus status = engine->rhi->AcquireNextImage(&idx);
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
        const WindowOps* ops = runtime_default_window_ops();
        vk_handle_runtime_input(engine, ops);
        vk_update_camera_key_state(engine, ops);

        core_engine_update(&engine->core, &engine->currentInput, 0.25f);

        engine->currentInput.mouseDeltaX = 0.0f;
        engine->currentInput.mouseDeltaY = 0.0f;
        engine->currentInput.scrollDelta = 0.0f;
    }

    UBOData uboData;
    uboData.modelRotation = glm::mat4(1.0f);
    glm::mat4 view = glm::lookAt(engine->core.camera.position, engine->core.camera.position + engine->core.camera.front, engine->core.camera.up);

    uint32_t renderWidth;
    uint32_t renderHeight;
    engine->rhi->GetResolution(&renderWidth, &renderHeight);
    const float aspect = static_cast<float>(renderWidth) / static_cast<float>(renderHeight);

    glm::mat4 proj = glm::perspective(glm::radians(engine->core.camera.zoom), aspect, kNearPlane, kFarPlane);
    proj[1][1] *= -1;
    uboData.vp = proj * view;

    glm::mat4 skyboxView = glm::mat4(glm::mat3(view));
    glm::mat4 skyboxProj = glm::perspective(glm::radians(kLegacyDefaultFov), aspect, kNearPlane, kFarPlane);
    skyboxProj[1][1] *= -1;
    uboData.invViewProj = glm::inverse(skyboxProj * skyboxView);

    uboData.cameraPosEnvLod = glm::vec4(engine->core.camera.position, engine->core.envLod);
    uboData.debugParams = glm::vec4(static_cast<float>(engine->core.iblDebugMode), engine->core.iblDebugScale, engine->core.billboardMode ? 1.0f : 0.0f, 0.0f);
    uboData.postParams1 = glm::vec4(engine->core.exposure, engine->core.saturation, engine->core.contrast, engine->core.gamma);
    uboData.postParams2 = glm::vec4(engine->core.gain, engine->core.offset, engine->core.wbTemp, engine->core.wbTint);
    uboData.view = view;
    uboData.proj = proj;

    uboData.windowSize = glm::vec4(static_cast<float>(renderWidth), static_cast<float>(renderHeight), 0.0f, 0.0f);

    engine->rhi->UpdateUBO(uboData);

    {
        SVK_TRACY_ZONE_SCOPED("Frame CPU Record");
        if (!engine->rhi->BeginFrame()) {
            return false;
        }

        engine->rhi->CollectProfiling();

        {
            SVK_RHI_GPU_ZONE(gpuFrameZone, engine->rhi, "GPU Frame");

            engine->rhi->BeginDebugLabel("Render_Frame_Graphics", 1.0f, 0.5f, 0.0f);

            engine->rhi->BeginDebugLabel("RenderPass_Begin_And_Bindings", 1.0f, 0.8f, 0.2f);
            engine->rhi->BeginRenderPass();
            engine->rhi->BindGlobalDescriptor();
            engine->rhi->EndDebugLabel();

            {
                SVK_RHI_GPU_ZONE(gpuSkyboxZone, engine->rhi, "GPU Skybox");
                engine->rhi->BeginDebugLabel("Render_Skybox_EnvMap", 0.2f, 0.5f, 1.0f);
                if (engine->core.showEnvmap) {
                    engine->rhi->BindPipeline(PipelineType::Skybox);
                    engine->rhi->Draw(3, 1);
                }
                engine->rhi->EndDebugLabel();
            }

            {
                SVK_RHI_GPU_ZONE(gpuSphereZone, engine->rhi, "GPU Spheres");
                engine->rhi->BeginDebugLabel("Render_Spheres_Instanced", 0.0f, 1.0f, 0.4f);
                if (engine->core.billboardMode) {
                    const glm::vec3 camPos = engine->core.camera.position;
                    std::sort(engine->core.billboardInstances.begin(), engine->core.billboardInstances.end(),
                              [&camPos](const BillboardInstance& a, const BillboardInstance& b) {
                                  glm::vec3 da = a.pos - camPos;
                                  glm::vec3 db = b.pos - camPos;
                                  return glm::dot(da, da) > glm::dot(db, db);
                              });

                    engine->rhi->UpdateBillboardInstances(engine->core.billboardInstances.data(), engine->core.billboardInstances.size());

                    engine->rhi->BindPipeline(PipelineType::Billboard);
                    engine->rhi->BindMeshBuffers(true);
                    engine->rhi->Draw(6, static_cast<uint32_t>(engine->core.billboardInstances.size()));
                } else {
                    engine->rhi->BindPipeline(engine->core.wireframeMode ? PipelineType::Wireframe : PipelineType::Graphics);
                    engine->rhi->BindMeshBuffers(false);
                    engine->rhi->DrawIndexed(engine->indexCount, kGridSize * kGridSize);
                }

                if (engine->core.wireframeMode && engine->core.billboardMode) {
                    DebugPushConstant dp = {};
                    dp.model = glm::mat4(1.0f);
                    dp.radius = 1.0f;

                    engine->rhi->BindPipeline(PipelineType::DebugTriangle);
                    dp.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.1f);
                    dp.mode = 1;
                    dp.stippled = 2;
                    engine->rhi->PushDebugConstants(&dp, sizeof(DebugPushConstant));
                    engine->rhi->Draw(6, kGridSize * kGridSize);

                    engine->rhi->BindPipeline(PipelineType::DebugLine);
                    dp.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
                    dp.mode = 1;
                    dp.stippled = 0;
                    engine->rhi->PushDebugConstants(&dp, sizeof(DebugPushConstant));
                    engine->rhi->Draw(8, kGridSize * kGridSize);

                    dp.color = glm::vec4(1.0f, 1.0f, 0.0f, 0.5f);
                    dp.mode = 0;
                    dp.stippled = 1;
                    engine->rhi->PushDebugConstants(&dp, sizeof(DebugPushConstant));
                    engine->rhi->Draw(24, kGridSize * kGridSize);
                }
                engine->rhi->EndDebugLabel();
            }

            engine->rhi->EndRenderPass();
            engine->rhi->EndDebugLabel();
        }

        engine->rhi->EndFrame();
    }

    {
        SVK_TRACY_ZONE_SCOPED("Frame CPU Submit and Present");
        SwapchainStatus status = engine->rhi->SubmitAndPresent(idx);
        if (status == SwapchainStatus::NeedRecreate) {
            return recreateSwapchain(engine);
        }
        return status == SwapchainStatus::Ok;
    }
}
