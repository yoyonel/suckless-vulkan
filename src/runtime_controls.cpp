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

AppResult runtime_toggle_fullscreen(EngineState* state, const WindowOps* ops) {
    if (!state->core.window.isFullscreen) {
        ops->get_window_pos(state->window, &state->core.window.windowedPosX, &state->core.window.windowedPosY);
        ops->get_window_size(state->window, &state->core.window.windowedWidth, &state->core.window.windowedHeight);

        GLFWmonitor* monitor = ops->get_primary_monitor();
        if (monitor == nullptr) {
            return AppResult::ErrorRuntime;
        }

        const GLFWvidmode* mode = ops->get_video_mode(monitor);
        if (mode == nullptr) {
            return AppResult::ErrorRuntime;
        }

        ops->set_window_monitor(state->window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        state->core.window.isFullscreen = true;
        LOG_INFO("runtime", "Mode fullscreen active");
        return AppResult::Success;
    }

    ops->set_window_monitor(state->window, nullptr, state->core.window.windowedPosX, state->core.window.windowedPosY, state->core.window.windowedWidth,
                            state->core.window.windowedHeight, 0);
    state->core.window.isFullscreen = false;
    LOG_INFO("runtime", "Mode fenetre active");
    return AppResult::Success;
}

void runtime_update_controls(EngineState* state, const WindowOps* ops) {
    state->currentInput.pausePressed = runtime_is_key_pressed_once(state->window, GLFW_KEY_P, &state->core.inputTracking.pauseKeyWasDown, ops);
    state->currentInput.cameraResetPressed = runtime_is_key_pressed_once(state->window, GLFW_KEY_SPACE, &state->core.inputTracking.cameraResetKeyWasDown, ops);
    state->currentInput.resetPressed = runtime_is_key_pressed_once(state->window, GLFW_KEY_R, &state->core.inputTracking.resetKeyWasDown, ops);
    state->currentInput.speedUpPressed = runtime_is_key_pressed_once(state->window, GLFW_KEY_UP, &state->core.inputTracking.speedUpKeyWasDown, ops);
    state->currentInput.speedDownPressed = runtime_is_key_pressed_once(state->window, GLFW_KEY_DOWN, &state->core.inputTracking.speedDownKeyWasDown, ops);

    if (runtime_is_key_pressed_once(state->window, GLFW_KEY_F11, &state->core.inputTracking.fullscreenKeyWasDown, ops)) {
        if (runtime_toggle_fullscreen(state, ops) == AppResult::Success) {
            state->core.lastFrameTimestamp = std::chrono::steady_clock::now();
        }
    }

    if (runtime_is_key_pressed_once(state->window, GLFW_KEY_ESCAPE, &state->core.inputTracking.escapeKeyWasDown, ops)) {
        ops->set_window_should_close(state->window, GLFW_TRUE);
    }
}
