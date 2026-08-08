#include "runtime_controls.h"

#include "app_log.h"
#include "camera.h"

#include <chrono>

namespace {

int default_get_key(GLFWwindow* window, int key) {
    return glfwGetKey(window, key);
}

void default_set_window_should_close(GLFWwindow* window, int value) {
    glfwSetWindowShouldClose(window, value);
}

GLFWmonitor* default_get_primary_monitor() {
    return glfwGetPrimaryMonitor();
}

const GLFWvidmode* default_get_video_mode(GLFWmonitor* monitor) {
    return glfwGetVideoMode(monitor);
}

void default_get_window_pos(GLFWwindow* window, int* xpos, int* ypos) {
    glfwGetWindowPos(window, xpos, ypos);
}

void default_get_window_size(GLFWwindow* window, int* width, int* height) {
    glfwGetWindowSize(window, width, height);
}

void default_set_window_monitor(GLFWwindow* window, GLFWmonitor* monitor, int xpos, int ypos, int width, int height, int refresh_rate) {
    glfwSetWindowMonitor(window, monitor, xpos, ypos, width, height, refresh_rate);
}

const WindowOps k_default_ops = {
    default_get_key,        default_set_window_should_close, default_get_primary_monitor, default_get_video_mode,
    default_get_window_pos, default_get_window_size,         default_set_window_monitor,
};

} // namespace

const WindowOps* runtime_default_window_ops() {
    return &k_default_ops;
}

bool runtime_is_key_pressed_once(GLFWwindow* window, int key, bool* wasDown, const WindowOps* ops) {
    const bool isDown = ops->get_key(window, key) == GLFW_PRESS;
    const bool pressedOnce = isDown && !(*wasDown);
    *wasDown = isDown;
    return pressedOnce;
}

bool runtime_toggle_fullscreen(VulkanEngine* engine, const WindowOps* ops) {
    if (!engine->appState->core.isFullscreen) {
        ops->get_window_pos(engine->appState->window, &engine->appState->core.windowedPosX, &engine->appState->core.windowedPosY);
        ops->get_window_size(engine->appState->window, &engine->appState->core.windowedWidth, &engine->appState->core.windowedHeight);

        GLFWmonitor* monitor = ops->get_primary_monitor();
        if (monitor == nullptr) {
            return false;
        }

        const GLFWvidmode* mode = ops->get_video_mode(monitor);
        if (mode == nullptr) {
            return false;
        }

        ops->set_window_monitor(engine->appState->window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        engine->appState->core.isFullscreen = true;
        LOG_INFO("runtime", "Mode fullscreen active");
        return true;
    }

    ops->set_window_monitor(engine->appState->window, nullptr, engine->appState->core.windowedPosX, engine->appState->core.windowedPosY,
                            engine->appState->core.windowedWidth, engine->appState->core.windowedHeight, 0);
    engine->appState->core.isFullscreen = false;
    LOG_INFO("runtime", "Mode fenetre active");
    return true;
}

void runtime_update_controls(VulkanEngine* engine, const WindowOps* ops) {
    engine->appState->currentInput.pausePressed =
        runtime_is_key_pressed_once(engine->appState->window, GLFW_KEY_P, &engine->appState->core.pauseKeyWasDown, ops);
    engine->appState->currentInput.cameraResetPressed =
        runtime_is_key_pressed_once(engine->appState->window, GLFW_KEY_SPACE, &engine->appState->core.cameraResetKeyWasDown, ops);
    engine->appState->currentInput.resetPressed =
        runtime_is_key_pressed_once(engine->appState->window, GLFW_KEY_R, &engine->appState->core.resetKeyWasDown, ops);
    engine->appState->currentInput.speedUpPressed =
        runtime_is_key_pressed_once(engine->appState->window, GLFW_KEY_UP, &engine->appState->core.speedUpKeyWasDown, ops);
    engine->appState->currentInput.speedDownPressed =
        runtime_is_key_pressed_once(engine->appState->window, GLFW_KEY_DOWN, &engine->appState->core.speedDownKeyWasDown, ops);

    if (runtime_is_key_pressed_once(engine->appState->window, GLFW_KEY_F11, &engine->appState->core.fullscreenKeyWasDown, ops)) {
        if (runtime_toggle_fullscreen(engine, ops)) {
            engine->appState->core.lastFrameTimestamp = std::chrono::steady_clock::now();
        }
    }

    if (runtime_is_key_pressed_once(engine->appState->window, GLFW_KEY_ESCAPE, &engine->appState->core.escapeKeyWasDown, ops)) {
        ops->set_window_should_close(engine->appState->window, GLFW_TRUE);
    }
}
