#include "vk_engine_runtime.h"

#include "app_log.h"
#include "vk_engine_envmap.h"

#include <GLFW/glfw3.h>

namespace {

constexpr float kEnvLodStep = 0.5f;

bool is_shift_down(GLFWwindow* window) {
    return glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
}

} // namespace

void vk_update_camera_key_state(VulkanEngine* engine) {
    engine->camera.moveForward = glfwGetKey(engine->window, GLFW_KEY_W) == GLFW_PRESS;
    engine->camera.moveBackward = glfwGetKey(engine->window, GLFW_KEY_S) == GLFW_PRESS;
    engine->camera.moveLeft = glfwGetKey(engine->window, GLFW_KEY_A) == GLFW_PRESS;
    engine->camera.moveRight = glfwGetKey(engine->window, GLFW_KEY_D) == GLFW_PRESS;
    engine->camera.moveUp = glfwGetKey(engine->window, GLFW_KEY_Q) == GLFW_PRESS;
    engine->camera.moveDown = glfwGetKey(engine->window, GLFW_KEY_E) == GLFW_PRESS;
}

void vk_handle_runtime_input(VulkanEngine* engine) {
    const bool cDown = glfwGetKey(engine->window, GLFW_KEY_C) == GLFW_PRESS;
    if (cDown && !engine->cameraToggleKeyWasDown) {
        engine->cameraEnabled = !engine->cameraEnabled;
        glfwSetInputMode(engine->window, GLFW_CURSOR, engine->cameraEnabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        engine->camera.firstMouse = true;
        LOG_INFO("runtime", "Camera souris: %s", engine->cameraEnabled ? "ON" : "OFF");
    }
    engine->cameraToggleKeyWasDown = cDown;

    const bool kDown = glfwGetKey(engine->window, GLFW_KEY_K) == GLFW_PRESS;
    if (kDown && !engine->showEnvmapToggleKeyWasDown) {
        engine->showEnvmap = !engine->showEnvmap;
        LOG_INFO("runtime", "Skybox: %s", engine->showEnvmap ? "ON" : "OFF");
    }
    engine->showEnvmapToggleKeyWasDown = kDown;

    const bool shiftDown = is_shift_down(engine->window);

    const bool pgUpDown = glfwGetKey(engine->window, GLFW_KEY_PAGE_UP) == GLFW_PRESS;
    if (pgUpDown && !engine->envPageUpKeyWasDown) {
        if (shiftDown) {
            vk_adjust_env_lod(engine, kEnvLodStep);
        } else {
            vk_switch_environment_texture(engine, 1);
        }
    }
    engine->envPageUpKeyWasDown = pgUpDown;

    const bool pgDownDown = glfwGetKey(engine->window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS;
    if (pgDownDown && !engine->envPageDownKeyWasDown) {
        if (shiftDown) {
            vk_adjust_env_lod(engine, -kEnvLodStep);
        } else {
            vk_switch_environment_texture(engine, -1);
        }
    }
    engine->envPageDownKeyWasDown = pgDownDown;
}

void vk_mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    auto* engine = static_cast<VulkanEngine*>(glfwGetWindowUserPointer(window));
    if (engine == nullptr || !engine->cameraEnabled) {
        return;
    }

    if (engine->camera.firstMouse) {
        engine->camera.lastMouseX = xpos;
        engine->camera.lastMouseY = ypos;
        engine->camera.firstMouse = false;
        return;
    }

    const float xOffset = static_cast<float>(xpos - engine->camera.lastMouseX);
    const float yOffset = static_cast<float>(ypos - engine->camera.lastMouseY);
    engine->camera.lastMouseX = xpos;
    engine->camera.lastMouseY = ypos;

    camera_process_mouse(&engine->camera, xOffset, yOffset);
}

void vk_scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    (void)xoffset;
    auto* engine = static_cast<VulkanEngine*>(glfwGetWindowUserPointer(window));
    if (engine == nullptr) {
        return;
    }
    camera_process_scroll(&engine->camera, static_cast<float>(yoffset));
}
