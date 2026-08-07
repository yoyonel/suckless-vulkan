#include "app_log.h"
#include "runtime_controls.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

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
    FakeWindowOpsState state = {};
    g_fake = &state;
    const WindowOps ops = make_fake_ops();

    VulkanEngine engine = {};
    engine.window = reinterpret_cast<GLFWwindow*>(0x2);
    engine.animationSpeed = 1.0f;
    engine.animationTimeSeconds = 3.0f;

    state.keyStates[GLFW_KEY_ESCAPE] = GLFW_PRESS;
    runtime_update_controls(&engine, &ops);
    check(stats, state.shouldCloseCalled, "ESC should request window close");
    check(stats, state.shouldCloseValue == GLFW_TRUE, "ESC should set GLFW_TRUE close flag");

    state.keyStates[GLFW_KEY_ESCAPE] = GLFW_RELEASE;
    runtime_update_controls(&engine, &ops);

    state.keyStates[GLFW_KEY_F11] = GLFW_PRESS;
    runtime_update_controls(&engine, &ops);
    check(stats, engine.isFullscreen, "F11 should switch to fullscreen");
    check(stats, state.setWindowMonitorCallCount == 1, "Entering fullscreen should call set_window_monitor once");
    check(stats, state.lastMonitor != nullptr, "Fullscreen should pass a monitor");
    check(stats, state.lastW == 1920 && state.lastH == 1080, "Fullscreen should use monitor resolution");

    state.keyStates[GLFW_KEY_F11] = GLFW_RELEASE;
    runtime_update_controls(&engine, &ops);

    state.keyStates[GLFW_KEY_F11] = GLFW_PRESS;
    runtime_update_controls(&engine, &ops);
    check(stats, !engine.isFullscreen, "Second F11 should return to window mode");
    check(stats, state.setWindowMonitorCallCount == 2, "Windowed restore should call set_window_monitor");
    check(stats, state.lastMonitor == nullptr, "Windowed restore should pass null monitor");
    check(stats, state.lastX == state.windowX && state.lastY == state.windowY, "Windowed restore should use saved position");
    check(stats, state.lastW == state.windowW && state.lastH == state.windowH, "Windowed restore should use saved size");

    camera_init(&engine.camera);
    engine.camera.position = glm::vec3(1.0f, 2.0f, 3.0f);

    state.keyStates[GLFW_KEY_P] = GLFW_PRESS;
    runtime_update_controls(&engine, &ops);
    check(stats, engine.animationPaused, "P should toggle pause on");
    state.keyStates[GLFW_KEY_P] = GLFW_RELEASE;
    runtime_update_controls(&engine, &ops);

    state.keyStates[GLFW_KEY_SPACE] = GLFW_PRESS;
    runtime_update_controls(&engine, &ops);
    check(stats, engine.camera.position.z == 20.0f, "Space should reset camera position (z=20)");
    check(stats, engine.camera.position.x == 0.0f, "Space should reset camera position (x=0)");
    state.keyStates[GLFW_KEY_SPACE] = GLFW_RELEASE;
    runtime_update_controls(&engine, &ops);

    state.keyStates[GLFW_KEY_UP] = GLFW_PRESS;
    runtime_update_controls(&engine, &ops);
    check(stats, engine.animationSpeed > 1.2f, "Up should increase speed");
    state.keyStates[GLFW_KEY_UP] = GLFW_RELEASE;
    runtime_update_controls(&engine, &ops);

    engine.animationSpeed = 0.05f;
    state.keyStates[GLFW_KEY_DOWN] = GLFW_PRESS;
    runtime_update_controls(&engine, &ops);
    check(stats, engine.animationSpeed >= 0.1f, "Down should clamp speed to minimum");
    state.keyStates[GLFW_KEY_DOWN] = GLFW_RELEASE;
    runtime_update_controls(&engine, &ops);

    engine.animationTimeSeconds = 9.0f;
    engine.animationSpeed = 2.0f;
    state.keyStates[GLFW_KEY_R] = GLFW_PRESS;
    runtime_update_controls(&engine, &ops);
    check(stats, engine.animationTimeSeconds == 0.0f, "R should reset animation time");
    check(stats, engine.animationSpeed == 1.0f, "R should restore default animation speed");

    g_fake = nullptr;
}

} // namespace

int main() {
    TestStats stats = {};

    test_logging(&stats);
    test_runtime_controls(&stats);

    if (stats.failed != 0) {
        LOG_ERROR("test", "logic tests failed: %d failed / %d passed", stats.failed, stats.passed);
        return 1;
    }

    LOG_INFO("test", "logic tests passed: %d", stats.passed);
    return 0;
}
