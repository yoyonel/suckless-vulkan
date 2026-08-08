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
    if (!engine->core.isFullscreen) {
        ops->get_window_pos(engine->window, &engine->core.windowedPosX, &engine->core.windowedPosY);
        ops->get_window_size(engine->window, &engine->core.windowedWidth, &engine->core.windowedHeight);

        GLFWmonitor* monitor = ops->get_primary_monitor();
        if (monitor == nullptr) {
            return false;
        }

        const GLFWvidmode* mode = ops->get_video_mode(monitor);
        if (mode == nullptr) {
            return false;
        }

        ops->set_window_monitor(engine->window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        engine->core.isFullscreen = true;
        LOG_INFO("runtime", "Mode fullscreen active");
        return true;
    }

    ops->set_window_monitor(engine->window, nullptr, engine->core.windowedPosX, engine->core.windowedPosY, engine->core.windowedWidth,
                            engine->core.windowedHeight, 0);
    engine->core.isFullscreen = false;
    LOG_INFO("runtime", "Mode fenetre active");
    return true;
}

void runtime_update_controls(VulkanEngine* engine, const WindowOps* ops) {
    engine->currentInput.pausePressed = runtime_is_key_pressed_once(engine->window, GLFW_KEY_P, &engine->core.pauseKeyWasDown, ops);
    engine->currentInput.cameraResetPressed = runtime_is_key_pressed_once(engine->window, GLFW_KEY_SPACE, &engine->core.cameraResetKeyWasDown, ops);
    engine->currentInput.resetPressed = runtime_is_key_pressed_once(engine->window, GLFW_KEY_R, &engine->core.resetKeyWasDown, ops);
    engine->currentInput.speedUpPressed = runtime_is_key_pressed_once(engine->window, GLFW_KEY_UP, &engine->core.speedUpKeyWasDown, ops);
    engine->currentInput.speedDownPressed = runtime_is_key_pressed_once(engine->window, GLFW_KEY_DOWN, &engine->core.speedDownKeyWasDown, ops);

    if (runtime_is_key_pressed_once(engine->window, GLFW_KEY_F11, &engine->core.fullscreenKeyWasDown, ops)) {
        if (runtime_toggle_fullscreen(engine, ops)) {
            engine->core.lastFrameTimestamp = std::chrono::steady_clock::now();
        }
    }

    if (runtime_is_key_pressed_once(engine->window, GLFW_KEY_ESCAPE, &engine->core.escapeKeyWasDown, ops)) {
        ops->set_window_should_close(engine->window, GLFW_TRUE);
    }
}
