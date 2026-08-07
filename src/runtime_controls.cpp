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
    if (!engine->isFullscreen) {
        ops->get_window_pos(engine->window, &engine->windowedPosX, &engine->windowedPosY);
        ops->get_window_size(engine->window, &engine->windowedWidth, &engine->windowedHeight);

        GLFWmonitor* monitor = ops->get_primary_monitor();
        if (monitor == nullptr) {
            return false;
        }

        const GLFWvidmode* mode = ops->get_video_mode(monitor);
        if (mode == nullptr) {
            return false;
        }

        ops->set_window_monitor(engine->window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        engine->isFullscreen = true;
        LOG_INFO("runtime", "Mode fullscreen active");
        return true;
    }

    ops->set_window_monitor(engine->window, nullptr, engine->windowedPosX, engine->windowedPosY, engine->windowedWidth, engine->windowedHeight, 0);
    engine->isFullscreen = false;
    LOG_INFO("runtime", "Mode fenetre active");
    return true;
}

void runtime_update_controls(VulkanEngine* engine, const WindowOps* ops) {
    if (runtime_is_key_pressed_once(engine->window, GLFW_KEY_P, &engine->pauseKeyWasDown, ops)) {
        engine->animationPaused = !engine->animationPaused;
        LOG_INFO("runtime", "Animation %s", engine->animationPaused ? "en pause" : "reprise");
    }

    if (runtime_is_key_pressed_once(engine->window, GLFW_KEY_SPACE, &engine->cameraResetKeyWasDown, ops)) {
        camera_init(&engine->camera);
        LOG_INFO("runtime", "Position camera reinitialisee");
    }

    if (runtime_is_key_pressed_once(engine->window, GLFW_KEY_R, &engine->resetKeyWasDown, ops)) {
        engine->animationTimeSeconds = 0.0f;
        engine->animationSpeed = 1.0f;
        LOG_INFO("runtime", "Animation reinitialisee");
    }

    if (runtime_is_key_pressed_once(engine->window, GLFW_KEY_UP, &engine->speedUpKeyWasDown, ops)) {
        engine->animationSpeed *= 1.25f;
        LOG_INFO("runtime", "Vitesse animation: %.2fx", engine->animationSpeed);
    }

    if (runtime_is_key_pressed_once(engine->window, GLFW_KEY_DOWN, &engine->speedDownKeyWasDown, ops)) {
        engine->animationSpeed *= 0.8f;
        engine->animationSpeed = engine->animationSpeed < 0.1f ? 0.1f : engine->animationSpeed;
        LOG_INFO("runtime", "Vitesse animation: %.2fx", engine->animationSpeed);
    }

    if (runtime_is_key_pressed_once(engine->window, GLFW_KEY_F11, &engine->fullscreenKeyWasDown, ops)) {
        if (runtime_toggle_fullscreen(engine, ops)) {
            engine->lastFrameTimestamp = std::chrono::steady_clock::now();
        }
    }

    if (runtime_is_key_pressed_once(engine->window, GLFW_KEY_ESCAPE, &engine->escapeKeyWasDown, ops)) {
        ops->set_window_should_close(engine->window, GLFW_TRUE);
    }
}
