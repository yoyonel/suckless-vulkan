#include "app_log.h"
#include "core_engine.h"
#include "runtime_controls.h"
#include "vk_engine_runtime.h"

#define UPDATE_CONTROLS()                                                                                                                                      \
    runtime_update_controls(&appState, &ops);                                                                                                                  \
    vk_handle_runtime_input(&engine, &ops);                                                                                                                    \
    vk_update_camera_key_state(&engine, &ops);                                                                                                                 \
    core_engine_update(&appState.core, &appState.currentInput, 0.16f);
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Mocks for vk_engine_runtime.cpp dependencies not linked in logic_tests
void vk_adjust_env_lod(VulkanEngine* engine, float step) {
    (void)engine;
    (void)step;
}
void vk_switch_environment_texture(VulkanEngine* engine, int dir) {
    (void)engine;
    (void)dir;
}
void vk_ibl_export_maps(VulkanEngine* engine) {
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

WindowOps make_fake_ops() {
    WindowOps ops = {};
    ops.get_key = fake_get_key;
    ops.set_window_should_close = fake_set_window_should_close;
    ops.get_primary_monitor = fake_get_primary_monitor;
    ops.get_video_mode = fake_get_video_mode;
    ops.get_window_pos = fake_get_window_pos;
    ops.get_window_size = fake_get_window_size;
    ops.set_window_monitor = fake_set_window_monitor;
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
    check(stats, !runtime_toggle_fullscreen(&appState, &ops), "Toggle fullscreen should fail if no monitor");
    fakeState.hasMonitor = true;
    fakeState.hasVideoMode = false;
    check(stats, !runtime_toggle_fullscreen(&appState, &ops), "Toggle fullscreen should fail if no video mode");
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

} // namespace

int main() {
    TestStats stats = {};

    test_logging(&stats);
    test_runtime_controls(&stats);
    test_vk_engine_runtime(&stats);

    if (stats.failed != 0) {
        LOG_ERROR("test", "logic tests failed: %d failed / %d passed", stats.failed, stats.passed);
        return 1;
    }

    LOG_INFO("test", "logic tests passed: %d", stats.passed);
    return 0;
}
