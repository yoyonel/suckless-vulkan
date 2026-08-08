#include "vk_engine_runtime.h"

#include "app_log.h"
#include "runtime_controls.h"
#include "vk_engine_envmap.h"
#include "vk_engine_ibl.h"
#include "vk_engine_init.h"

#include <GLFW/glfw3.h>

namespace {

bool is_shift_down(GLFWwindow* window, const WindowOps* ops) {
    return ops->get_key(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || ops->get_key(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
}

bool is_key_pressed_once(GLFWwindow* window, int key, bool* wasDown, const WindowOps* ops) {
    const bool isDown = ops->get_key(window, key) == GLFW_PRESS;
    const bool pressedOnce = isDown && !(*wasDown);
    *wasDown = isDown;
    return pressedOnce;
}

void handle_camera_and_envmap_toggles(VulkanEngine* engine, const WindowOps* ops) {
    engine->appState->currentInput.cameraTogglePressed =
        is_key_pressed_once(engine->appState->window, GLFW_KEY_C, &engine->appState->core.cameraToggleKeyWasDown, ops);
    engine->appState->currentInput.showEnvmapTogglePressed =
        is_key_pressed_once(engine->appState->window, GLFW_KEY_K, &engine->appState->core.showEnvmapToggleKeyWasDown, ops);
    engine->appState->currentInput.billboardPressed =
        is_key_pressed_once(engine->appState->window, GLFW_KEY_B, &engine->appState->core.billboardKeyWasDown, ops);
    engine->appState->currentInput.wireframePressed =
        is_key_pressed_once(engine->appState->window, GLFW_KEY_Z, &engine->appState->core.wireframeKeyWasDown, ops);
}

void handle_env_navigation(VulkanEngine* engine, bool shiftDown, const WindowOps* ops) {
    engine->appState->currentInput.envPageUpPressed =
        is_key_pressed_once(engine->appState->window, GLFW_KEY_PAGE_UP, &engine->appState->core.envPageUpKeyWasDown, ops);
    engine->appState->currentInput.envPageDownPressed =
        is_key_pressed_once(engine->appState->window, GLFW_KEY_PAGE_DOWN, &engine->appState->core.envPageDownKeyWasDown, ops);
    engine->appState->currentInput.envShiftDown = shiftDown;

    if (engine->appState->currentInput.envPageUpPressed && !shiftDown) {
        vk_switch_environment_texture(engine, 1);
    }
    if (engine->appState->currentInput.envPageDownPressed && !shiftDown) {
        vk_switch_environment_texture(engine, -1);
    }
}

void handle_ibl_debug_inputs(VulkanEngine* engine, const WindowOps* ops) {
    for (int digit = 0; digit <= 9; ++digit) {
        const int key = GLFW_KEY_0 + digit;
        engine->appState->currentInput.iblDebugDigitPressed[digit] =
            is_key_pressed_once(engine->appState->window, key, &engine->appState->core.iblDebugDigitKeyWasDown[digit], ops);
    }

    engine->appState->currentInput.iblDebugPrevPressed =
        is_key_pressed_once(engine->appState->window, GLFW_KEY_LEFT_BRACKET, &engine->appState->core.iblDebugPrevKeyWasDown, ops);
    engine->appState->currentInput.iblDebugNextPressed =
        is_key_pressed_once(engine->appState->window, GLFW_KEY_RIGHT_BRACKET, &engine->appState->core.iblDebugNextKeyWasDown, ops);
    engine->appState->currentInput.iblDebugF6Pressed =
        is_key_pressed_once(engine->appState->window, GLFW_KEY_F6, &engine->appState->core.iblDebugF6KeyWasDown, ops);
    engine->appState->currentInput.iblExportPressed =
        is_key_pressed_once(engine->appState->window, GLFW_KEY_O, &engine->appState->core.iblExportKeyWasDown, ops);

    if (engine->appState->currentInput.iblExportPressed) {
        vk_ibl_export_maps(engine);
        LOG_INFO("runtime", "IBL maps export requested (/tmp/ibl_tests/vk)");
    }
}

void handle_postprocess_inputs(VulkanEngine* engine, const WindowOps* ops) {
    engine->appState->currentInput.postExposureAddDown = ops->get_key(engine->appState->window, GLFW_KEY_KP_ADD) == GLFW_PRESS;
    engine->appState->currentInput.postExposureSubDown = ops->get_key(engine->appState->window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS;

    engine->appState->currentInput.postResetPressed =
        is_key_pressed_once(engine->appState->window, GLFW_KEY_0, &engine->appState->core.postResetKeyWasDown, ops) ||
        is_key_pressed_once(engine->appState->window, GLFW_KEY_KP_0, &engine->appState->core.postResetKeyWasDown, ops);
}

} // namespace

void vk_update_camera_key_state(VulkanEngine* engine, const WindowOps* ops) {
    engine->appState->currentInput.moveForward = ops->get_key(engine->appState->window, GLFW_KEY_W) == GLFW_PRESS;
    engine->appState->currentInput.moveBackward = ops->get_key(engine->appState->window, GLFW_KEY_S) == GLFW_PRESS;
    engine->appState->currentInput.moveLeft = ops->get_key(engine->appState->window, GLFW_KEY_A) == GLFW_PRESS;
    engine->appState->currentInput.moveRight = ops->get_key(engine->appState->window, GLFW_KEY_D) == GLFW_PRESS;
    engine->appState->currentInput.moveUp = ops->get_key(engine->appState->window, GLFW_KEY_Q) == GLFW_PRESS;
    engine->appState->currentInput.moveDown = ops->get_key(engine->appState->window, GLFW_KEY_E) == GLFW_PRESS;
}

void vk_handle_runtime_input(VulkanEngine* engine, const WindowOps* ops) {
    handle_camera_and_envmap_toggles(engine, ops);

    const bool shiftDown = is_shift_down(engine->appState->window, ops);
    handle_env_navigation(engine, shiftDown, ops);
    handle_ibl_debug_inputs(engine, ops);
    handle_postprocess_inputs(engine, ops);

    if (is_key_pressed_once(engine->appState->window, GLFW_KEY_V, &engine->appState->core.vsyncKeyWasDown, ops)) {
        engine->appState->core.vsync = !engine->appState->core.vsync;
        LOG_INFO("runtime", "VSync toggle: %s (recreating swapchain...)", engine->appState->core.vsync ? "ON" : "OFF");
        vk_recreate_swapchain(engine);
    }
}

void vk_mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    auto* engine = static_cast<VulkanEngine*>(glfwGetWindowUserPointer(window));
    if (engine == nullptr) {
        return;
    }

    if (engine->appState->core.camera.firstMouse) {
        engine->appState->core.camera.lastMouseX = xpos;
        engine->appState->core.camera.lastMouseY = ypos;
        engine->appState->core.camera.firstMouse = false;
        return;
    }

    engine->appState->currentInput.mouseDeltaX += static_cast<float>(xpos - engine->appState->core.camera.lastMouseX);
    engine->appState->currentInput.mouseDeltaY += static_cast<float>(ypos - engine->appState->core.camera.lastMouseY);
    engine->appState->core.camera.lastMouseX = xpos;
    engine->appState->core.camera.lastMouseY = ypos;
}

void vk_scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    (void)xoffset;
    auto* engine = static_cast<VulkanEngine*>(glfwGetWindowUserPointer(window));
    if (engine == nullptr) {
        return;
    }
    engine->appState->currentInput.scrollDelta += static_cast<float>(yoffset);
}
