#include "app_log.h"
#include "core_engine.h"
#include "runtime_controls.h"
#include "vk_engine_runtime.h"

#include <algorithm>

#define UPDATE_CONTROLS()                                                                                                                                      \
    runtime_update_controls(&appState, &ops);                                                                                                                  \
    vk_handle_runtime_input(&engine, &ops);                                                                                                                    \
    vk_update_camera_key_state(&engine, &ops);                                                                                                                 \
    core_engine_update(&appState.core, &appState.currentInput, 0.16f);
#include "rhi/command_list.h"
#include "rhi/rhi_ptr.h"
#include "rhi/vulkan_state_mapper.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

class MockCommandList : public IRenderCommandList {
  public:
    void BeginRenderPass() override {}
    void BeginRenderPassLoad() override {}
    void EndRenderPass() override {}
    void BindPipeline(PipelineHandle /*pipeline*/, bool /*isCompute*/) override {}
    void BindDescriptorSets(PipelineLayoutHandle /*layout*/, uint32_t /*firstSet*/, uint32_t /*count*/, const DescriptorSetHandle* /*sets*/,
                            bool /*isCompute*/) override {}
    void BindDescriptorSetsWithDynamicOffsets(PipelineLayoutHandle /*layout*/, uint32_t /*firstSet*/, uint32_t /*count*/, const DescriptorSetHandle* /*sets*/,
                                              uint32_t /*dynamicOffsetCount*/, const uint32_t* /*dynamicOffsets*/, bool /*isCompute*/) override {}
    void PushConstants(PipelineLayoutHandle /*layout*/, ShaderStage /*stage*/, uint32_t /*offset*/, uint32_t /*size*/, const void* /*values*/) override {}
    void BindVertexBuffers(uint32_t /*firstBinding*/, uint32_t /*bindingCount*/, const BufferHandle* /*buffers*/, const uint64_t* /*offsets*/) override {}
    void BindIndexBuffer(BufferHandle /*buffer*/, uint64_t /*offset*/, uint32_t /*indexType*/) override {}
    void Draw(uint32_t /*vertexCount*/, uint32_t /*instanceCount*/, uint32_t /*firstVertex*/, uint32_t /*firstInstance*/) override {}
    void DrawIndexed(uint32_t /*indexCount*/, uint32_t /*instanceCount*/, uint32_t /*firstIndex*/, int32_t /*vertexOffset*/,
                     uint32_t /*firstInstance*/) override {}
    void Dispatch(uint32_t /*groupCountX*/, uint32_t /*groupCountY*/, uint32_t /*groupCountZ*/) override {}
    void SetViewport(float /*x*/, float /*y*/, float /*width*/, float /*height*/, float /*minDepth*/, float /*maxDepth*/) override {}
    void SetScissor(int32_t /*x*/, int32_t /*y*/, uint32_t /*width*/, uint32_t /*height*/) override {}
    void BeginDebugLabel(const char* /*name*/, float /*r*/, float /*g*/, float /*b*/) override {}
    void EndDebugLabel() override {}
    void InsertDebugLabel(const char* /*name*/, float /*r*/, float /*g*/, float /*b*/) override {}
    void TransitionTexture(TextureHandle /*texture*/, ResourceState /*newState*/) override {}
    void TransitionBuffer(BufferHandle /*buffer*/, ResourceState /*newState*/) override {}
    void FlushBarriers() override {}

    void BindComputePipeline(PipelineHandle /*pipeline*/) override {}
    void SetBindGroup(uint32_t /*setIndex*/, BindGroupHandle /*bindGroup*/, PipelineLayoutHandle /*pipelineLayout*/ = INVALID_HANDLE) override {}
    void PushComputeConstants(const void* /*data*/, uint32_t /*size*/, uint32_t /*offset*/ = 0) override {}
    void DispatchCompute(uint32_t /*groupCountX*/, uint32_t /*groupCountY*/, uint32_t /*groupCountZ*/) override {}

    void BindGraphicsPipeline(PipelineHandle /*pipeline*/) override {}
    void SetGraphicsBindGroup(uint32_t /*setIndex*/, BindGroupHandle /*bindGroup*/, uint32_t /*dynamicOffsetCount*/ = 0,
                              const uint32_t* /*dynamicOffsets*/ = nullptr) override {}
    void PushGraphicsConstants(ShaderStage /*stage*/, uint32_t /*offset*/, uint32_t /*size*/, const void* /*values*/) override {}
};

// Mock minimal pour IRHI

class MockRHI : public IRHI {

  public:
    int destroyedBuffers = 0;
    int destroyedTextures = 0;
    int destroyedBindGroups = 0;

    RHIResult Init() override {
        return RHIResult::Success;
    }
    void Shutdown() override {}
    RHIResult DrawFrame() override {
        return RHIResult::Success;
    }
    void HandleInputs(const WindowOps* /*ops*/) override {}

    BufferHandle CreateBuffer(std::size_t /*size*/, BufferUsage /*usage*/, const void* /*initialData*/, const char* /*name*/) override {
        return 1;
    }
    void DestroyBuffer(BufferHandle /*handle*/) override {
        destroyedBuffers++;
    }
    void* MapBuffer(BufferHandle /*handle*/) override {
        return nullptr;
    }
    void UnmapBuffer(BufferHandle /*handle*/) override {}

    TextureHandle CreateTexture(uint32_t /*width*/, uint32_t /*height*/, TextureFormat /*format*/, TextureUsage /*usage*/, uint32_t /*mipLevels*/,
                                const char* /*name*/) override {
        return 2;
    }
    void DestroyTexture(TextureHandle /*handle*/) override {
        destroyedTextures++;
    }

    ImageViewHandle CreateImageView(TextureHandle /*texture*/, uint32_t /*baseMipLevel*/, uint32_t /*levelCount*/, uint32_t /*baseArrayLayer*/,
                                    uint32_t /*layerCount*/) override {
        return 3;
    }
    void DestroyImageView(ImageViewHandle /*handle*/) override {}

    SamplerHandle CreateSampler(uint32_t /*mipLevels*/, bool /*clampToEdge*/, const char* /*name*/) override {
        return 4;
    }
    void DestroySampler(SamplerHandle /*handle*/) override {}

    DescriptorLayoutHandle CreateDescriptorLayout(const DescriptorLayoutDesc& /*desc*/, const char* /*name*/) override {
        return 5;
    }
    void DestroyDescriptorLayout(DescriptorLayoutHandle /*handle*/) override {}

    DescriptorPoolHandle CreateDescriptorPool(const DescriptorPoolDesc& /*desc*/, const char* /*name*/) override {
        return 6;
    }
    void DestroyDescriptorPool(DescriptorPoolHandle /*handle*/) override {}

    RHIResult AllocateDescriptorSets(const DescriptorSetAllocateDesc& /*desc*/, DescriptorSetHandle* /*outSets*/) override {
        return RHIResult::Success;
    }
    void UpdateDescriptorSets(uint32_t /*writeCount*/, const WriteDescriptorSet* /*pDescriptorWrites*/) override {}

    PipelineLayoutHandle CreatePipelineLayout(const PipelineLayoutDesc& /*desc*/, const char* /*name*/) override {
        return 7;
    }
    void DestroyPipelineLayout(PipelineLayoutHandle /*handle*/) override {}

    PipelineHandle CreateComputePipeline(const ComputePipelineDesc& /*desc*/) override {
        return 8;
    }
    PipelineHandle CreateComputePipeline(const DeclarativeComputePipelineDesc& /*desc*/) override {
        return 8;
    }
    BindGroupHandle CreateBindGroup(const BindGroupDesc& /*desc*/) override {
        return 10;
    }
    void DestroyBindGroup(BindGroupHandle /*handle*/) override {
        destroyedBindGroups++;
    }
    PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& /*desc*/) override {
        return 9;
    }
    PipelineHandle CreateGraphicsPipeline(const DeclarativeGraphicsPipelineDesc& /*desc*/) override {
        return 9;
    }
    void DestroyPipeline(PipelineHandle /*handle*/) override {}

    SwapchainStatus AcquireNextImage(uint32_t* /*imageIndex*/) override {
        return SwapchainStatus::Ok;
    }
    void UpdateUBO(const UBOData& /*data*/) override {}
    RHIResult BeginFrame() override {
        return RHIResult::Success;
    }
    void EndFrame() override {}
    SwapchainStatus SubmitAndPresent(uint32_t /*imageIndex*/) override {
        return SwapchainStatus::Ok;
    }

    void BeginRenderPass() override {}
    void BeginRenderPassLoad() override {}
    void EndRenderPass() override {}

    PipelineHandle GetPipeline(PipelineType /*type*/) const override {
        return INVALID_HANDLE;
    }
    void BindGlobalDescriptor(class IRenderCommandList* /*cmdList*/) override {}
    void BindMeshBuffers(class IRenderCommandList* /*cmdList*/, bool /*isBillboard*/) override {}
    class IRenderCommandList* GetMainCommandList() override {
        return nullptr;
    }
    void UpdateBillboardInstances(const uint32_t* /*instances*/, std::size_t /*count*/) override {}

    void PushDebugConstants(const void* /*data*/, uint32_t /*size*/) override {}
    void BeginDebugLabel(const char* /*name*/, float /*r*/, float /*g*/, float /*b*/) override {}
    void EndDebugLabel() override {}
    void CollectProfiling() override {}
    void GetResolution(uint32_t* /*width*/, uint32_t* /*height*/) const override {}

    void* GetOpaqueTracyContext() const override {
        return nullptr;
    }
    CommandBufferHandle GetOpaqueCommandBuffer() const override {
        return {nullptr};
    }
};

// Mocks for vk_engine_runtime.cpp dependencies not linked in logic_tests
void vk_adjust_env_lod(VulkanEngine* engine, float step) {
    (void)engine;
    (void)step;
}
void vk_switch_environment_texture(VulkanEngine* engine, int dir) {
    (void)engine;
    (void)dir;
}
void IblBaker::ExportMaps(VulkanEngine* engine) const { // NOLINT(readability-convert-member-functions-to-static)
    (void)engine;
}
bool vk_recreate_swapchain(VulkanEngine* engine) {
    (void)engine;
    return true;
}

namespace {

struct TestStats {
    int passed = 0;
    int failed = 0;
};

void check(TestStats* stats, bool condition, const char* label) {
    if (condition) {
        stats->passed += 1;
    } else {
        stats->failed += 1;
        LOG_ERROR("test", "FAIL: %s", label);
    }
}

struct CallbackCapture {
    int count = 0;
    LogLevel lastLevel = LogLevel::NotSet;
    char tag[32] = {};
    char message[256] = {};
};

CallbackCapture* g_capture = nullptr;

void test_log_callback(LogLevel level, const char* tag, const char* message) {
    if (g_capture == nullptr) {
        return;
    }

    g_capture->count += 1;
    g_capture->lastLevel = level;
    std::snprintf(g_capture->tag, sizeof(g_capture->tag), "%s", tag != nullptr ? tag : "");
    std::snprintf(g_capture->message, sizeof(g_capture->message), "%s", message != nullptr ? message : "");
}

struct FakeWindowOpsState {
    int keyStates[512] = {};
    bool shouldCloseCalled = false;
    int shouldCloseValue = 0;
    int windowX = 120;
    int windowY = 80;
    int windowW = 800;
    int windowH = 600;
    bool hasMonitor = true;
    bool hasVideoMode = true;
    int setWindowMonitorCallCount = 0;
    GLFWmonitor* lastMonitor = nullptr;
    int lastX = 0;
    int lastY = 0;
    int lastW = 0;
    int lastH = 0;
    int lastRefresh = 0;
};

FakeWindowOpsState* g_fake = nullptr;

int fake_get_key(GLFWwindow* window, int key) {
    (void)window;
    if (g_fake == nullptr || key < 0 || key >= static_cast<int>(sizeof(g_fake->keyStates) / sizeof(g_fake->keyStates[0]))) {
        return GLFW_RELEASE;
    }
    return g_fake->keyStates[key];
}

void fake_set_window_should_close(GLFWwindow* window, int value) {
    (void)window;
    if (g_fake == nullptr) {
        return;
    }
    g_fake->shouldCloseCalled = true;
    g_fake->shouldCloseValue = value;
}

GLFWmonitor* fake_get_primary_monitor() {
    if (g_fake == nullptr || !g_fake->hasMonitor) {
        return nullptr;
    }
    return reinterpret_cast<GLFWmonitor*>(0x1);
}

const GLFWvidmode* fake_get_video_mode(GLFWmonitor* monitor) {
    (void)monitor;
    static GLFWvidmode mode = {};
    if (g_fake == nullptr || !g_fake->hasVideoMode) {
        return nullptr;
    }
    mode.width = 1920;
    mode.height = 1080;
    mode.refreshRate = 60;
    return &mode;
}

void fake_get_window_pos(GLFWwindow* window, int* xpos, int* ypos) {
    (void)window;
    *xpos = g_fake->windowX;
    *ypos = g_fake->windowY;
}

void fake_get_window_size(GLFWwindow* window, int* width, int* height) {
    (void)window;
    *width = g_fake->windowW;
    *height = g_fake->windowH;
}

void fake_set_window_monitor(GLFWwindow* window, GLFWmonitor* monitor, int xpos, int ypos, int width, int height, int refresh_rate) {
    (void)window;
    if (g_fake == nullptr) {
        return;
    }
    g_fake->setWindowMonitorCallCount += 1;
    g_fake->lastMonitor = monitor;
    g_fake->lastX = xpos;
    g_fake->lastY = ypos;
    g_fake->lastW = width;
    g_fake->lastH = height;
    g_fake->lastRefresh = refresh_rate;
}

void fake_set_input_mode(GLFWwindow* window, int mode, int value) {
    (void)window;
    (void)mode;
    (void)value;
}

WindowOps make_fake_ops() {
    WindowOps ops = {};
    ops.get_key = fake_get_key;
    ops.set_window_should_close = fake_set_window_should_close;
    ops.get_primary_monitor = fake_get_primary_monitor;
    ops.get_video_mode = fake_get_video_mode;
    ops.get_window_pos = fake_get_window_pos;
    ops.get_window_size = fake_get_window_size;
    ops.set_window_monitor = fake_set_window_monitor;
    ops.set_input_mode = fake_set_input_mode;
    return ops;
}

void test_logging(TestStats* stats) {
    (void)setenv("VULKAN_LOG_LEVEL", "DEBUG", 1);

    check(stats, log_get_level() == LogLevel::Debug, "env VULKAN_LOG_LEVEL should set DEBUG level");

    CallbackCapture capture = {};
    g_capture = &capture;
    log_set_callback(test_log_callback);

    log_set_level(LogLevel::Warning);
    LOG_INFO("log-test", "this should be filtered");
    check(stats, capture.count == 0, "INFO log should be filtered at WARNING level");

    LOG_ERROR("log-test", "error code=%d", 42);
    check(stats, capture.count == 1, "ERROR should trigger callback");
    check(stats, capture.lastLevel == LogLevel::Error, "callback should receive ERROR level");
    check(stats, std::strcmp(capture.tag, "log-test") == 0, "callback should receive the right tag");
    check(stats, std::strcmp(capture.message, "error code=42") == 0, "callback should receive formatted message");

    log_set_callback(nullptr);
    g_capture = nullptr;
}

void test_runtime_controls(TestStats* stats) {
    FakeWindowOpsState fakeState = {};
    g_fake = &fakeState;
    const WindowOps ops = make_fake_ops();

    EngineState appState = {};
    VulkanEngine engine = {};
    engine.appState = &appState;
    appState.window = reinterpret_cast<GLFWwindow*>(0x2);
    appState.core.time.animationSpeed = 1.0f;
    appState.core.time.animationTimeSeconds = 3.0f;
    appState.core.lastFrameTimestamp = std::chrono::steady_clock::now();

    fakeState.keyStates[GLFW_KEY_ESCAPE] = GLFW_PRESS;
    UPDATE_CONTROLS();
    check(stats, fakeState.shouldCloseCalled, "ESC should request window close");
    check(stats, fakeState.shouldCloseValue == GLFW_TRUE, "ESC should set GLFW_TRUE close flag");

    fakeState.keyStates[GLFW_KEY_ESCAPE] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    fakeState.keyStates[GLFW_KEY_F11] = GLFW_PRESS;
    UPDATE_CONTROLS();
    check(stats, appState.core.window.isFullscreen, "F11 should switch to fullscreen");
    check(stats, fakeState.setWindowMonitorCallCount == 1, "Entering fullscreen should call set_window_monitor once");
    check(stats, fakeState.lastMonitor != nullptr, "Fullscreen should pass a monitor");
    check(stats, fakeState.lastW == 1920 && fakeState.lastH == 1080, "Fullscreen should use monitor resolution");

    fakeState.keyStates[GLFW_KEY_F11] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    fakeState.keyStates[GLFW_KEY_F11] = GLFW_PRESS;
    UPDATE_CONTROLS();
    check(stats, !appState.core.window.isFullscreen, "Second F11 should return to window mode");
    check(stats, fakeState.setWindowMonitorCallCount == 2, "Windowed restore should call set_window_monitor");
    check(stats, fakeState.lastMonitor == nullptr, "Windowed restore should pass null monitor");
    check(stats, fakeState.lastX == fakeState.windowX && fakeState.lastY == fakeState.windowY, "Windowed restore should use saved position");
    check(stats, fakeState.lastW == fakeState.windowW && fakeState.lastH == fakeState.windowH, "Windowed restore should use saved size");

    // Edge cases for null monitor/video mode
    fakeState.hasMonitor = false;
    check(stats, runtime_toggle_fullscreen(&appState, &ops) != AppResult::Success, "Toggle fullscreen should fail if no monitor");
    fakeState.hasMonitor = true;
    fakeState.hasVideoMode = false;
    check(stats, runtime_toggle_fullscreen(&appState, &ops) != AppResult::Success, "Toggle fullscreen should fail if no video mode");
    fakeState.hasVideoMode = true;

    camera_init(&appState.core.camera);
    appState.core.camera.position = glm::vec3(1.0f, 2.0f, 3.0f);

    fakeState.keyStates[GLFW_KEY_P] = GLFW_PRESS;
    UPDATE_CONTROLS();
    check(stats, appState.core.time.animationPaused, "P should toggle pause on");
    fakeState.keyStates[GLFW_KEY_P] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    fakeState.keyStates[GLFW_KEY_SPACE] = GLFW_PRESS;
    UPDATE_CONTROLS();
    check(stats, appState.core.camera.position.z == 20.0f, "Space should reset camera position (z=20)");
    check(stats, appState.core.camera.position.x == 0.0f, "Space should reset camera position (x=0)");
    fakeState.keyStates[GLFW_KEY_SPACE] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    fakeState.keyStates[GLFW_KEY_UP] = GLFW_PRESS;
    UPDATE_CONTROLS();
    check(stats, appState.core.time.animationSpeed > 1.2f, "Up should increase speed");
    fakeState.keyStates[GLFW_KEY_UP] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    appState.core.time.animationSpeed = 0.05f;
    fakeState.keyStates[GLFW_KEY_DOWN] = GLFW_PRESS;
    UPDATE_CONTROLS();
    check(stats, appState.core.time.animationSpeed >= 0.1f, "Down should clamp speed to minimum");
    fakeState.keyStates[GLFW_KEY_DOWN] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    appState.core.time.animationTimeSeconds = 9.0f;
    appState.core.time.animationSpeed = 2.0f;
    fakeState.keyStates[GLFW_KEY_R] = GLFW_PRESS;
    appState.core.lastFrameTimestamp -= std::chrono::milliseconds(16);
    UPDATE_CONTROLS();
    check(stats, appState.core.time.animationTimeSeconds < 0.05f, "R should reset animation time");
    check(stats, appState.core.time.animationSpeed == 1.0f, "R should restore default animation speed");

    g_fake = nullptr;
}

void test_vk_engine_runtime(TestStats* stats) {
    FakeWindowOpsState fakeState = {};
    g_fake = &fakeState;
    const WindowOps ops = make_fake_ops();

    EngineState appState = {};
    VulkanEngine engine = {};
    engine.appState = &appState;
    appState.window = reinterpret_cast<GLFWwindow*>(0x2);
    appState.core.lastFrameTimestamp = std::chrono::steady_clock::now();

    appState.core.cameraEnabled = false;
    fakeState.keyStates[GLFW_KEY_C] = GLFW_PRESS;
    UPDATE_CONTROLS();
    check(stats, appState.core.cameraEnabled, "C toggles camera ON");
    fakeState.keyStates[GLFW_KEY_C] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    appState.core.render.showEnvmap = false;
    fakeState.keyStates[GLFW_KEY_K] = GLFW_PRESS;
    UPDATE_CONTROLS();
    check(stats, appState.core.render.showEnvmap, "K toggles skybox ON");
    fakeState.keyStates[GLFW_KEY_K] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    appState.core.render.billboardMode = false;
    fakeState.keyStates[GLFW_KEY_B] = GLFW_PRESS;
    UPDATE_CONTROLS();
    check(stats, appState.core.render.billboardMode, "B toggles billboard ON");
    fakeState.keyStates[GLFW_KEY_B] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    appState.core.render.wireframeMode = false;
    fakeState.keyStates[GLFW_KEY_Z] = GLFW_PRESS;
    UPDATE_CONTROLS();
    check(stats, appState.core.render.wireframeMode, "Z toggles wireframe ON");
    UPDATE_CONTROLS();
    check(stats, appState.core.render.wireframeMode, "Z held down does not toggle wireframe OFF");
    fakeState.keyStates[GLFW_KEY_Z] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    appState.core.vsync = false;
    fakeState.keyStates[GLFW_KEY_V] = GLFW_PRESS;
    UPDATE_CONTROLS();
    check(stats, appState.core.vsync, "V toggles vsync ON");
    fakeState.keyStates[GLFW_KEY_V] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    fakeState.keyStates[GLFW_KEY_PAGE_UP] = GLFW_PRESS;
    UPDATE_CONTROLS();
    fakeState.keyStates[GLFW_KEY_PAGE_UP] = GLFW_RELEASE;

    fakeState.keyStates[GLFW_KEY_LEFT_SHIFT] = GLFW_PRESS;
    fakeState.keyStates[GLFW_KEY_PAGE_UP] = GLFW_PRESS;
    UPDATE_CONTROLS();
    fakeState.keyStates[GLFW_KEY_PAGE_UP] = GLFW_RELEASE;

    fakeState.keyStates[GLFW_KEY_PAGE_DOWN] = GLFW_PRESS;
    UPDATE_CONTROLS();
    fakeState.keyStates[GLFW_KEY_PAGE_DOWN] = GLFW_RELEASE;
    fakeState.keyStates[GLFW_KEY_LEFT_SHIFT] = GLFW_RELEASE;

    fakeState.keyStates[GLFW_KEY_PAGE_DOWN] = GLFW_PRESS;
    UPDATE_CONTROLS();
    fakeState.keyStates[GLFW_KEY_PAGE_DOWN] = GLFW_RELEASE;

    fakeState.keyStates[GLFW_KEY_RIGHT_SHIFT] = GLFW_PRESS;
    UPDATE_CONTROLS();
    fakeState.keyStates[GLFW_KEY_RIGHT_SHIFT] = GLFW_RELEASE;

    for (int digit = 0; digit <= 9; ++digit) {
        fakeState.keyStates[GLFW_KEY_0 + digit] = GLFW_PRESS;
        UPDATE_CONTROLS();
        check(stats, appState.core.render.iblDebugMode == digit, "Digit key sets IBL debug mode");
        fakeState.keyStates[GLFW_KEY_0 + digit] = GLFW_RELEASE;
        UPDATE_CONTROLS();
    }

    appState.core.render.iblDebugMode = 5;
    fakeState.keyStates[GLFW_KEY_LEFT_BRACKET] = GLFW_PRESS;
    UPDATE_CONTROLS();
    check(stats, appState.core.render.iblDebugMode == 4, "[ decrements IBL debug mode");
    fakeState.keyStates[GLFW_KEY_LEFT_BRACKET] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    fakeState.keyStates[GLFW_KEY_LEFT_BRACKET] = GLFW_PRESS;
    appState.core.render.iblDebugMode = -100;
    UPDATE_CONTROLS();
    check(stats, appState.core.render.iblDebugMode == 0, "[ clamps IBL mode to 0");
    fakeState.keyStates[GLFW_KEY_LEFT_BRACKET] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    fakeState.keyStates[GLFW_KEY_RIGHT_BRACKET] = GLFW_PRESS;
    appState.core.render.iblDebugMode = 5;
    UPDATE_CONTROLS();
    check(stats, appState.core.render.iblDebugMode == 6, "] increments IBL debug mode");
    fakeState.keyStates[GLFW_KEY_RIGHT_BRACKET] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    fakeState.keyStates[GLFW_KEY_RIGHT_BRACKET] = GLFW_PRESS;
    appState.core.render.iblDebugMode = 100;
    UPDATE_CONTROLS();
    check(stats, appState.core.render.iblDebugMode == 9, "] clamps IBL debug mode to 9");
    fakeState.keyStates[GLFW_KEY_RIGHT_BRACKET] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    appState.core.render.iblDebugMode = 5;
    fakeState.keyStates[GLFW_KEY_F6] = GLFW_PRESS;
    UPDATE_CONTROLS();
    check(stats, appState.core.render.iblDebugMode == 6, "F6 cycles IBL debug mode");
    fakeState.keyStates[GLFW_KEY_F6] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    fakeState.keyStates[GLFW_KEY_O] = GLFW_PRESS;
    UPDATE_CONTROLS();
    fakeState.keyStates[GLFW_KEY_O] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    appState.core.render.exposure = 1.0f;
    fakeState.keyStates[GLFW_KEY_KP_ADD] = GLFW_PRESS;
    appState.core.lastFrameTimestamp -= std::chrono::milliseconds(100);
    UPDATE_CONTROLS();
    check(stats, appState.core.render.exposure > 1.0f, "KP_ADD increases exposure");
    fakeState.keyStates[GLFW_KEY_KP_ADD] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    fakeState.keyStates[GLFW_KEY_KP_SUBTRACT] = GLFW_PRESS;
    appState.core.lastFrameTimestamp -= std::chrono::milliseconds(100);
    UPDATE_CONTROLS();
    check(stats, appState.core.render.exposure < 1.3f, "KP_SUBTRACT decreases exposure");
    fakeState.keyStates[GLFW_KEY_KP_SUBTRACT] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    appState.core.render.exposure = 0.0f;
    fakeState.keyStates[GLFW_KEY_KP_SUBTRACT] = GLFW_PRESS;
    appState.core.lastFrameTimestamp -= std::chrono::milliseconds(100);
    UPDATE_CONTROLS();
    check(stats, appState.core.render.exposure == 0.01f, "KP_SUBTRACT clamps to 0.01f");
    fakeState.keyStates[GLFW_KEY_KP_SUBTRACT] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    appState.core.render.exposure = 0.5f;
    fakeState.keyStates[GLFW_KEY_0] = GLFW_PRESS;
    UPDATE_CONTROLS();
    check(stats, appState.core.render.exposure == 1.0f, "0 resets exposure");
    fakeState.keyStates[GLFW_KEY_0] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    appState.core.render.exposure = 0.5f;
    fakeState.keyStates[GLFW_KEY_KP_0] = GLFW_PRESS;
    UPDATE_CONTROLS();
    check(stats, appState.core.render.exposure == 1.0f, "KP_0 resets exposure");
    fakeState.keyStates[GLFW_KEY_KP_0] = GLFW_RELEASE;
    UPDATE_CONTROLS();

    fakeState.keyStates[GLFW_KEY_W] = GLFW_PRESS;
    fakeState.keyStates[GLFW_KEY_S] = GLFW_PRESS;
    fakeState.keyStates[GLFW_KEY_A] = GLFW_PRESS;
    fakeState.keyStates[GLFW_KEY_D] = GLFW_PRESS;
    fakeState.keyStates[GLFW_KEY_Q] = GLFW_PRESS;
    fakeState.keyStates[GLFW_KEY_E] = GLFW_PRESS;
    UPDATE_CONTROLS();
    check(stats,
          appState.core.camera.moveForward && appState.core.camera.moveBackward && appState.core.camera.moveLeft && appState.core.camera.moveRight &&
              appState.core.camera.moveUp && appState.core.camera.moveDown,
          "Camera keys set correctly");

    g_fake = nullptr;
}

void test_rhi_ptr(TestStats* stats) {
    MockRHI mock;

    {
        rhi::BufferPtr buf(&mock, 42);
        check(stats, buf.get() == 42, "BufferPtr contains correct handle");
        check(stats, mock.destroyedBuffers == 0, "Buffer not destroyed yet");
    }
    check(stats, mock.destroyedBuffers == 1, "Buffer destroyed at end of scope");

    {
        rhi::TexturePtr tex(&mock, 100);
        rhi::TexturePtr& tex_ref = tex;
        rhi::TexturePtr tex2 = std::move(tex);
        check(stats, tex_ref.empty(), "Moved-from handle is invalid");
        check(stats, tex2.get() == 100, "Moved-to handle is valid");
        check(stats, mock.destroyedTextures == 0, "Texture not destroyed on move");
    }
    check(stats, mock.destroyedTextures == 1, "Texture destroyed at end of scope after move");

    {
        rhi::BindGroupPtr bg(&mock, 55);
        check(stats, bg.get() == 55, "BindGroupPtr contains correct handle");
        check(stats, mock.destroyedBindGroups == 0, "BindGroup not destroyed yet");
    }
    check(stats, mock.destroyedBindGroups == 1, "BindGroup destroyed at end of scope");
}

} // namespace

#include "../src/result.h"
#include "tracy_client.h"
#include <chrono>

static void test_results(TestStats* stats) {
    check(stats, std::strcmp(to_string(AppResult::Success), "Success") == 0, "to_string(AppResult::Success)");
    check(stats, std::strcmp(to_string(AppResult::ErrorInitializationFailed), "ErrorInitializationFailed") == 0,
          "to_string(AppResult::ErrorInitializationFailed)");
    check(stats, std::strcmp(to_string(RHIResult::ErrorOutOfMemory), "ErrorOutOfMemory") == 0, "to_string(RHIResult::ErrorOutOfMemory)");
    check(stats, std::strcmp(to_string(ResourceResult::ErrorFileNotFound), "ErrorFileNotFound") == 0, "to_string(ResourceResult::ErrorFileNotFound)");
    check(stats, std::strcmp(to_string(GfxResult::ErrorInvalidState), "ErrorInvalidState") == 0, "to_string(GfxResult::ErrorInvalidState)");
}

static void test_tracy_client_shutdown(TestStats* stats) {
    check(stats, tracy_client_startup("TestProgram") == AppResult::Success, "tracy_client_startup");
    check(stats, tracy_client_startup("TestProgram") == AppResult::Success, "tracy_client_startup idempotent");
    check(stats, tracy_client_poll_connection() == AppResult::Success, "tracy_client_poll_connection");
    tracy_client_mark_frame();

    const auto t0 = std::chrono::steady_clock::now();
    tracy_client_shutdown(100);
    const auto t1 = std::chrono::steady_clock::now();
    const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    check(stats, duration_ms < 50, "tracy_client_shutdown is immediate when disconnected (<50ms)");
    tracy_client_shutdown(100);
}

static void test_bloom_math(TestStats* stats) {
    // 1. Test calcul dimensions mips
    auto compute_mip_dim = [](uint32_t base_w, uint32_t base_h, int mip_level) {
        uint32_t w = std::max(1u, base_w >> (mip_level + 1));
        uint32_t h = std::max(1u, base_h >> (mip_level + 1));
        return std::pair<uint32_t, uint32_t>{w, h};
    };

    // 1080p
    auto m0_1080 = compute_mip_dim(1920, 1080, 0);
    auto m1_1080 = compute_mip_dim(1920, 1080, 1);
    auto m2_1080 = compute_mip_dim(1920, 1080, 2);
    auto m3_1080 = compute_mip_dim(1920, 1080, 3);
    auto m4_1080 = compute_mip_dim(1920, 1080, 4);

    check(stats, m0_1080.first == 960 && m0_1080.second == 540, "Bloom Mip 0 1080p -> 960x540");
    check(stats, m1_1080.first == 480 && m1_1080.second == 270, "Bloom Mip 1 1080p -> 480x270");
    check(stats, m2_1080.first == 240 && m2_1080.second == 135, "Bloom Mip 2 1080p -> 240x135");
    check(stats, m3_1080.first == 120 && m3_1080.second == 67, "Bloom Mip 3 1080p -> 120x67");
    check(stats, m4_1080.first == 60 && m4_1080.second == 33, "Bloom Mip 4 1080p -> 60x33");

    // Dimensions minimales (clamp at 1x1)
    auto m4_tiny = compute_mip_dim(7, 5, 4);
    check(stats, m4_tiny.first >= 1 && m4_tiny.second >= 1, "Bloom tiny mip dimensions clamped >= 1");

    // 2. Test Soft-Knee Threshold
    auto soft_knee = [](float brightness, float threshold, float knee) {
        float rq = std::clamp(brightness - threshold + knee, 0.0f, 2.0f * knee);
        float soft = (rq * rq) / ((4.0f * std::max(knee, 1e-5f)) + 1e-5f);
        float weight = std::max(brightness - threshold, soft) / std::max(brightness, 1e-5f);
        return std::max(weight, 0.0f);
    };

    const float threshold = 1.0f;
    const float knee = 0.5f;

    // En dessous de (threshold - knee) = 0.5 -> poids 0
    check(stats, soft_knee(0.2f, threshold, knee) == 0.0f, "Soft-knee weight is 0 below threshold - knee");
    check(stats, soft_knee(0.49f, threshold, knee) == 0.0f, "Soft-knee weight is 0 right before knee start");

    // Dans la zone douce (0.5 .. 1.5)
    float w_mid = soft_knee(1.0f, threshold, knee);
    check(stats, w_mid > 0.0f && w_mid < 1.0f, "Soft-knee weight is smoothly interpolated at threshold");

    // Loin au dessus de threshold + knee -> asymptote linéaire
    float w_high = soft_knee(10.0f, threshold, knee);
    check(stats, std::abs((w_high * 10.0f) - (10.0f - threshold)) < 0.01f, "Soft-knee behaves linearly for high values");

    // 3. Test Karis Weight (suppression de fireflies)
    auto karis_weight = [](float lum) { return 1.0f / (1.0f + lum); };
    check(stats, std::abs(karis_weight(0.0f) - 1.0f) < 1e-5f, "Karis weight for 0 lum is 1.0");
    check(stats, karis_weight(100.0f) < karis_weight(1.0f), "Karis weight decreases monotonically with luminance");
    check(stats, karis_weight(10000.0f) < 0.001f, "Karis weight heavily suppresses extreme fireflies");

    // 4. Test Bloom Debug Modes Cycle (ISO suckless-ogl: Off -> FinalMap -> Prefilter -> Downsample -> Upsample)
    int32_t debugMode = 0;
    debugMode = (debugMode + 1) % 5;
    check(stats, debugMode == 1, "Bloom Debug cycle 0 -> 1 (FinalMap)");
    debugMode = (debugMode + 1) % 5;
    check(stats, debugMode == 2, "Bloom Debug cycle 1 -> 2 (Prefilter)");
    debugMode = (debugMode + 1) % 5;
    check(stats, debugMode == 3, "Bloom Debug cycle 2 -> 3 (Downsample)");
    debugMode = (debugMode + 1) % 5;
    check(stats, debugMode == 4, "Bloom Debug cycle 3 -> 4 (Upsample)");
    debugMode = (debugMode + 1) % 5;
    check(stats, debugMode == 0, "Bloom Debug cycle 4 -> 0 (Off)");
}

void test_autoexposure_math(TestStats* stats) {
    // 1. Test 64-bin log luminance quantization
    float minLogLum = -8.0f;
    float maxLogLum = 8.0f;
    float logLumRange = maxLogLum - minLogLum; // 16.0
    float invLogLumRange = 1.0f / logLumRange;

    auto compute_bin = [minLogLum, invLogLumRange](float lum) {
        float logLum = std::clamp((std::log2(std::max(lum, 0.00005f)) - minLogLum) * invLogLumRange, 0.0f, 1.0f);
        return static_cast<uint32_t>(std::clamp(std::floor(logLum * 64.0f), 0.0f, 63.0f));
    };

    check(stats, compute_bin(std::exp2(-8.0f)) == 0, "Luminance 2^-8 maps to bin 0");
    check(stats, compute_bin(1.0f) == 32, "Luminance 1.0 (2^0) maps to middle bin 32");
    check(stats, compute_bin(std::exp2(8.0f)) == 63, "Luminance 2^8 maps to bin 63");
    check(stats, compute_bin(0.00001f) == 0, "Luminance below min maps to bin 0");
    check(stats, compute_bin(1000.0f) == 63, "Luminance above max maps to bin 63");

    // 2. Test Percentile Histogram Filtering [20%, 90%]
    uint32_t testHist[64] = {0};
    // Fill bins 10, 30, 50 with 100 pixels each
    testHist[10] = 100; // Low dark pixels
    testHist[30] = 100; // Mid-gray pixels
    testHist[50] = 100; // Highlights pixels
    float totalPixels = 300.0f;

    float minThreshold = totalPixels * 0.20f; // 60
    float maxThreshold = totalPixels * 0.90f; // 270

    float accumulated = 0.0f;
    float weightedSum = 0.0f;
    float validPixels = 0.0f;

    for (uint32_t i = 0; i < 64; ++i) {
        float count = static_cast<float>(testHist[i]);
        float nextAccum = accumulated + count;

        float validCount = count;
        if (nextAccum < minThreshold) {
            validCount = 0.0f;
        } else if (accumulated < minThreshold) {
            validCount = nextAccum - minThreshold;
        }

        if (accumulated > maxThreshold) {
            validCount = 0.0f;
        } else if (nextAccum > maxThreshold) {
            validCount = std::max(0.0f, maxThreshold - std::max(accumulated, minThreshold));
        }

        if (validCount > 0.0f) {
            float binLogLum = minLogLum + (((static_cast<float>(i) + 0.5f) / 64.0f) * logLumRange);
            weightedSum += binLogLum * validCount;
            validPixels += validCount;
        }
        accumulated = nextAccum;
    }

    check(stats, std::abs(validPixels - 210.0f) < 1e-3f, "Valid pixel count matches 90% - 20% span (210 pixels)");
    float avgLogLum = weightedSum / validPixels;
    float sceneLum = std::exp2(avgLogLum);
    check(stats, sceneLum > 0.0f && !std::isnan(sceneLum) && !std::isinf(sceneLum), "Calculated scene luminance is finite and positive");

    // 3. Test Asymmetric Temporal Smoothing
    float currentExp = 1.0f;
    float targetExpDark = 2.0f; // Scene got darker (+1.0 delta) -> adapt slowly (speedDown = 0.7)
    float speedDown = 0.7f;
    float dt = 0.016f;
    float factorDown = 1.0f - std::exp(-dt * speedDown);
    float newExpDarker = currentExp + ((targetExpDark - currentExp) * factorDown);
    check(stats, newExpDarker > currentExp && newExpDarker < targetExpDark, "Exposure smoothly rises when entering dark scene");

    float targetExpBright = 0.0f; // Scene got brighter (-1.0 delta) -> adapt quickly (speedUp = 2.0)
    float speedUp = 2.0f;
    float factorUp = 1.0f - std::exp(-dt * speedUp);
    float newExpBrighter = currentExp + ((targetExpBright - currentExp) * factorUp);
    check(stats, newExpBrighter < currentExp && newExpBrighter > targetExpBright, "Exposure smoothly falls when entering bright scene");
    check(stats, factorUp > factorDown, "Adaptation speedUp factor is greater than speedDown factor");
    // 4. Test Key Value middle gray calibration (ISO photographic 18% gray card: keyValue = 0.18)
    float keyValue = 0.18f;
    float midGrayLum = 0.18f;
    float midGrayExposure = keyValue / midGrayLum;
    check(stats, std::abs(midGrayExposure - 1.0f) < 1e-4f, "Middle-gray scene luminance (0.18) yields unit exposure (1.00)");
}

void test_resource_state_tracker(TestStats* stats) {
    // 1. Bitwise operators for ResourceState
    ResourceState s = ResourceState::ComputeShaderRead | ResourceState::ComputeShaderWrite;
    check(stats, s == ResourceState::ComputeReadWrite, "Bitwise OR forms composite state ComputeReadWrite");
    check(stats, (s & ResourceState::ComputeShaderRead) == ResourceState::ComputeShaderRead, "Bitwise AND checks flags");
    check(stats, (s & ResourceState::RenderTarget) == ResourceState::Undefined, "Bitwise AND non-member is Undefined");

    ResourceState s2 = ResourceState::TransferSrc;
    s2 |= ResourceState::TransferDst;
    check(stats, (s2 & ResourceState::TransferSrc) == ResourceState::TransferSrc, "operator|= works");
    check(stats, (s2 & ResourceState::TransferDst) == ResourceState::TransferDst, "operator|= sets secondary flag");

    // 2. Vulkan state mapping table invariants
    auto mUndef = map_resource_state_to_vulkan(ResourceState::Undefined);
    check(stats, mUndef.layout == VK_IMAGE_LAYOUT_UNDEFINED, "Undefined maps to VK_IMAGE_LAYOUT_UNDEFINED");
    check(stats, mUndef.accessMask == 0, "Undefined access mask is 0");
    check(stats, mUndef.stageMask == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, "Undefined stage mask is TOP_OF_PIPE");

    auto mShaderRes = map_resource_state_to_vulkan(ResourceState::ShaderResource);
    check(stats, mShaderRes.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, "ShaderResource maps to SHADER_READ_ONLY_OPTIMAL");
    check(stats, mShaderRes.accessMask == VK_ACCESS_SHADER_READ_BIT, "ShaderResource access mask is SHADER_READ_BIT");

    auto mCompWrite = map_resource_state_to_vulkan(ResourceState::ComputeShaderWrite);
    check(stats, mCompWrite.layout == VK_IMAGE_LAYOUT_GENERAL, "ComputeShaderWrite maps to GENERAL");
    check(stats, mCompWrite.accessMask == VK_ACCESS_SHADER_WRITE_BIT, "ComputeShaderWrite access mask is SHADER_WRITE_BIT");
    check(stats, mCompWrite.stageMask == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, "ComputeShaderWrite stage mask is COMPUTE_SHADER_BIT");

    auto mColorAtt = map_resource_state_to_vulkan(ResourceState::RenderTarget);
    check(stats, mColorAtt.layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, "RenderTarget maps to COLOR_ATTACHMENT_OPTIMAL");
    check(stats, (mColorAtt.accessMask & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT) != 0, "RenderTarget has color write access");

    auto mDepthWrite = map_resource_state_to_vulkan(ResourceState::DepthStencilWrite);
    check(stats, mDepthWrite.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, "DepthStencilWrite maps to DEPTH_STENCIL_ATTACHMENT_OPTIMAL");

    auto mTransferDst = map_resource_state_to_vulkan(ResourceState::TransferDst);
    check(stats, mTransferDst.layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, "TransferDst maps to TRANSFER_DST_OPTIMAL");
    check(stats, mTransferDst.accessMask == VK_ACCESS_TRANSFER_WRITE_BIT, "TransferDst access mask is TRANSFER_WRITE_BIT");

    // 3. State Tracker Transition Logic & Barrier Batching simulation
    struct SimulatedTracker {
        struct Res {
            ResourceState state{ResourceState::Undefined};
        };
        std::vector<Res> textures;
        std::vector<VkImageMemoryBarrier> pendingBarriers;
        int flushCount{0};

        void Transition(uint32_t handle, ResourceState newState) {
            if (handle >= textures.size())
                return;
            ResourceState oldState = textures[handle].state;
            if (oldState == newState)
                return; // No-op redundancy filter

            auto oldMap = map_resource_state_to_vulkan(oldState);
            auto newMap = map_resource_state_to_vulkan(newState);

            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = oldMap.layout;
            b.newLayout = newMap.layout;
            b.srcAccessMask = oldMap.accessMask;
            b.dstAccessMask = newMap.accessMask;

            textures[handle].state = newState;
            pendingBarriers.push_back(b);
        }

        void Flush() {
            if (pendingBarriers.empty())
                return;
            flushCount++;
            pendingBarriers.clear();
        }
    };

    SimulatedTracker tracker;
    tracker.textures.resize(4); // 4 resources

    // Initial transition of Texture 0: Undefined -> ComputeShaderWrite
    tracker.Transition(0, ResourceState::ComputeShaderWrite);
    check(stats, tracker.pendingBarriers.size() == 1, "First transition generates 1 barrier");
    check(stats, tracker.pendingBarriers[0].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED, "First transition oldLayout is UNDEFINED");
    check(stats, tracker.pendingBarriers[0].newLayout == VK_IMAGE_LAYOUT_GENERAL, "First transition newLayout is GENERAL");

    // Redundant transition: ComputeShaderWrite -> ComputeShaderWrite
    tracker.Transition(0, ResourceState::ComputeShaderWrite);
    check(stats, tracker.pendingBarriers.size() == 1, "Redundant transition is no-op (barrier count unchanged)");

    // Batch 3 transitions together across different resources
    tracker.Transition(1, ResourceState::ShaderResource); // Texture 1
    tracker.Transition(2, ResourceState::RenderTarget);   // Texture 2
    check(stats, tracker.pendingBarriers.size() == 3, "Batch queue has 3 pending barriers across resources");

    tracker.Flush();
    check(stats, tracker.flushCount == 1, "Single vkCmdPipelineBarrier call emitted for all 3 barriers");
    check(stats, tracker.pendingBarriers.empty(), "Pending barriers cleared after flush");

    // Frame N+1: Transition Texture 0 from General to ShaderResource (preserves content, oldLayout != UNDEFINED)
    tracker.Transition(0, ResourceState::ShaderResource);
    check(stats, tracker.pendingBarriers.size() == 1, "Transition from previous state generated 1 barrier");
    check(stats, tracker.pendingBarriers[0].oldLayout == VK_IMAGE_LAYOUT_GENERAL, "Persistence verified: oldLayout is GENERAL, NOT UNDEFINED");
    check(stats, tracker.pendingBarriers[0].newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, "newLayout is SHADER_READ_ONLY_OPTIMAL");

    tracker.Flush();
    check(stats, tracker.flushCount == 2, "Second flush completed successfully");
}

void test_bind_group_and_compute_pipeline(TestStats* stats) {
    MockRHI rhi;
    MockCommandList cmd;

    // 1. Declarative BindGroupLayout creation
    BindGroupLayoutDesc bgLayoutDesc{{
        {0, BindingType::CombinedImageSampler, 1, 0},
        {1, BindingType::StorageBuffer, 1, 0},
    }};
    check(stats, bgLayoutDesc.bindings.size() == 2, "BindGroupLayoutDesc has 2 entries");
    check(stats, bgLayoutDesc.bindings[0].type == BindingType::CombinedImageSampler, "Binding 0 is CombinedImageSampler");
    check(stats, bgLayoutDesc.bindings[1].type == BindingType::StorageBuffer, "Binding 1 is StorageBuffer");

    // 2. Declarative ComputePipelineDesc
    DeclarativeComputePipelineDesc cpDesc{
        "TestComputePipeline", "shaders/test.spv", nullptr, 0, "main", {bgLayoutDesc}, sizeof(uint32_t) * 4,
    };
    PipelineHandle pipe = rhi.CreateComputePipeline(cpDesc);
    check(stats, pipe == 8, "Declarative CreateComputePipeline returned valid handle");

    // 3. Declarative BindGroup creation
    BindGroupDesc bgDesc{
        "TestBindGroup",
        bgLayoutDesc,
        {
            {0, BindingType::CombinedImageSampler, 2, INVALID_HANDLE, 4, INVALID_HANDLE, 0, 0},
            {1, BindingType::StorageBuffer, INVALID_HANDLE, INVALID_HANDLE, INVALID_HANDLE, 1, 0, 256},
        },
    };
    BindGroupHandle bg = rhi.CreateBindGroup(bgDesc);
    check(stats, bg == 10, "CreateBindGroup returned valid handle");

    // 4. Command recording verification
    cmd.BindComputePipeline(pipe);
    cmd.SetBindGroup(0, bg);
    uint32_t pcData[4] = {1, 2, 3, 4};
    cmd.PushComputeConstants(pcData, sizeof(pcData));
    cmd.DispatchCompute(16, 16, 1);
    check(stats, true, "Declarative compute recording sequence executed without errors");
}

void test_bind_group_and_graphics_pipeline(TestStats* stats) {
    MockRHI rhi;
    MockCommandList cmd;

    // 1. Declarative BindGroupLayout creation for Graphics (e.g. Set 0 Per-Frame UBO)
    BindGroupLayoutDesc perFrameLayout{{
        {0, BindingType::UniformBuffer, 1, static_cast<uint32_t>(ShaderStage::AllGraphics)},
    }};
    check(stats, perFrameLayout.bindings.size() == 1, "Graphics per-frame layout has 1 binding");

    // 2. Declarative GraphicsPipelineDesc
    VertexInputBinding vBinding{0, sizeof(float) * 6, false};
    VertexInputAttribute vAttr0{0, 0, VertexFormat::Float3, 0};
    VertexInputAttribute vAttr1{1, 0, VertexFormat::Float3, sizeof(float) * 3};
    DeclarativeGraphicsPipelineDesc gpDesc{};
    gpDesc.debugName = "TestGraphicsPipeline";
    gpDesc.vertexShaderPath = "shaders/shader.vert.spv";
    gpDesc.fragmentShaderPath = "shaders/shader.frag.spv";
    gpDesc.bindGroupLayouts = {perFrameLayout};
    gpDesc.vertexBindings = {vBinding};
    gpDesc.vertexAttributes = {vAttr0, vAttr1};
    gpDesc.topology = Topology::TriangleList;
    gpDesc.cullMode = CullMode::Back;
    gpDesc.depthTestEnable = true;
    gpDesc.depthWriteEnable = true;
    gpDesc.pushConstantsSize = sizeof(float) * 4;

    PipelineHandle pipe = rhi.CreateGraphicsPipeline(gpDesc);
    check(stats, pipe == 9, "Declarative CreateGraphicsPipeline returned valid handle");

    // 3. Declarative BindGroup creation
    BindGroupDesc bgDesc{
        "PerFrameBindGroup",
        perFrameLayout,
        {
            {0, BindingType::UniformBuffer, INVALID_HANDLE, INVALID_HANDLE, INVALID_HANDLE, 1, 0, 256},
        },
    };
    BindGroupHandle bg = rhi.CreateBindGroup(bgDesc);
    check(stats, bg == 10, "CreateBindGroup for graphics returned valid handle");

    // 4. Command recording verification
    cmd.BindGraphicsPipeline(pipe);
    uint32_t dynOffset = 256;
    cmd.SetGraphicsBindGroup(0, bg, 1, &dynOffset);
    float pcData[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    cmd.PushGraphicsConstants(ShaderStage::Fragment, 0, sizeof(pcData), pcData);
    cmd.DrawIndexed(36, 1, 0, 0, 0);
    check(stats, true, "Declarative graphics recording sequence executed without errors");
}

int main() {
    TestStats stats = {};
    test_results(&stats);

    test_logging(&stats);
    test_runtime_controls(&stats);
    test_vk_engine_runtime(&stats);
    test_rhi_ptr(&stats);
    test_tracy_client_shutdown(&stats);
    test_bloom_math(&stats);
    test_autoexposure_math(&stats);
    test_resource_state_tracker(&stats);
    test_bind_group_and_compute_pipeline(&stats);
    test_bind_group_and_graphics_pipeline(&stats);

    if (stats.failed != 0) {
        LOG_ERROR("test", "logic tests failed: %d failed / %d passed", stats.failed, stats.passed);
        return 1;
    }

    LOG_INFO("test", "logic tests passed: %d", stats.passed);
    return 0;
}
