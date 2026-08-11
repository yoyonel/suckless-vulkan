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

InputState get_key_state(GLFWwindow* window, int key, bool* wasDown, const WindowOps* ops) {
    const bool isDown = ops->get_key(window, key) == GLFW_PRESS;
    const bool pressedOnce = isDown && !(*wasDown);
    *wasDown = isDown;
    if (pressedOnce)
        return InputState::PressedOnce;
    if (isDown)
        return InputState::Pressed;
    return InputState::Released;
}

void handle_camera_and_envmap_toggles(VulkanEngine* engine, const WindowOps* ops) {
    engine->appState->currentInput.cameraTogglePressed =
        get_key_state(engine->appState->window, GLFW_KEY_C, &engine->appState->core.inputTracking.cameraToggleKeyWasDown, ops);

    if (engine->appState->currentInput.cameraTogglePressed == InputState::PressedOnce) {
        if (ops->set_input_mode) {
            bool newCameraState = !engine->appState->core.cameraEnabled;
            ops->set_input_mode(engine->appState->window, GLFW_CURSOR, newCameraState ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        }
    }
    engine->appState->currentInput.showEnvmapTogglePressed =
        get_key_state(engine->appState->window, GLFW_KEY_K, &engine->appState->core.inputTracking.showEnvmapToggleKeyWasDown, ops);
    engine->appState->currentInput.billboardPressed =
        get_key_state(engine->appState->window, GLFW_KEY_B, &engine->appState->core.inputTracking.billboardKeyWasDown, ops);
    engine->appState->currentInput.wireframePressed =
        get_key_state(engine->appState->window, GLFW_KEY_Z, &engine->appState->core.inputTracking.wireframeKeyWasDown, ops);
}

void handle_env_navigation(VulkanEngine* engine, bool shiftDown, const WindowOps* ops) {
    engine->appState->currentInput.envPageUpPressed =
        get_key_state(engine->appState->window, GLFW_KEY_PAGE_UP, &engine->appState->core.inputTracking.envPageUpKeyWasDown, ops);
    engine->appState->currentInput.envPageDownPressed =
        get_key_state(engine->appState->window, GLFW_KEY_PAGE_DOWN, &engine->appState->core.inputTracking.envPageDownKeyWasDown, ops);

    engine->appState->currentInput.envShiftDown = shiftDown;

    if (engine->appState->currentInput.envPageUpPressed == InputState::PressedOnce && !shiftDown) {
        vk_switch_environment_texture(engine, 1);
    }
    if (engine->appState->currentInput.envPageDownPressed == InputState::PressedOnce && !shiftDown) {
        vk_switch_environment_texture(engine, -1);
    }
}

void handle_ibl_debug_inputs(VulkanEngine* engine, const WindowOps* ops) {
    for (int digit = 0; digit <= 9; ++digit) {
        const int key = GLFW_KEY_0 + digit;
        engine->appState->currentInput.iblDebugDigitPressed[digit] =
            get_key_state(engine->appState->window, key, &engine->appState->core.inputTracking.iblDebugDigitKeyWasDown[digit], ops);
    }

    engine->appState->currentInput.iblDebugPrevPressed =
        get_key_state(engine->appState->window, GLFW_KEY_LEFT_BRACKET, &engine->appState->core.inputTracking.iblDebugPrevKeyWasDown, ops);
    engine->appState->currentInput.iblDebugNextPressed =
        get_key_state(engine->appState->window, GLFW_KEY_RIGHT_BRACKET, &engine->appState->core.inputTracking.iblDebugNextKeyWasDown, ops);
    engine->appState->currentInput.iblDebugF6Pressed =
        get_key_state(engine->appState->window, GLFW_KEY_F6, &engine->appState->core.inputTracking.iblDebugF6KeyWasDown, ops);
    engine->appState->currentInput.iblExportPressed =
        get_key_state(engine->appState->window, GLFW_KEY_O, &engine->appState->core.inputTracking.iblExportKeyWasDown, ops);

    if (engine->appState->currentInput.iblExportPressed == InputState::PressedOnce) {
        vk_ibl_export_maps(engine);
        LOG_INFO("runtime", "IBL maps export requested (/tmp/ibl_tests/vk)");
    }
}

void handle_postprocess_inputs(VulkanEngine* engine, const WindowOps* ops) {
    engine->appState->currentInput.postExposureAddDown = ops->get_key(engine->appState->window, GLFW_KEY_KP_ADD) == GLFW_PRESS;
    engine->appState->currentInput.postExposureSubDown = ops->get_key(engine->appState->window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS;

    InputState post1 = get_key_state(engine->appState->window, GLFW_KEY_0, &engine->appState->core.inputTracking.postResetKeyWasDown, ops);
    InputState post2 = get_key_state(engine->appState->window, GLFW_KEY_KP_0, &engine->appState->core.inputTracking.postResetKeyWasDown, ops);
    engine->appState->currentInput.postResetPressed =
        (post1 == InputState::PressedOnce || post2 == InputState::PressedOnce) ? InputState::PressedOnce : InputState::Released;
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

    if (get_key_state(engine->appState->window, GLFW_KEY_V, &engine->appState->core.inputTracking.vsyncKeyWasDown, ops) == InputState::PressedOnce) {
        engine->appState->core.vsync = !engine->appState->core.vsync;
        LOG_INFO("runtime", "VSync toggle: %s (recreating swapchain...)", engine->appState->core.vsync ? "ON" : "OFF");
        (void)vk_recreate_swapchain(engine);
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
