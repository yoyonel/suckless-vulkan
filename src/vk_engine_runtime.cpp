#include "vk_engine_runtime.h"

#include "app_log.h"
#include "vk_engine_envmap.h"
#include "vk_engine_ibl.h"

#include <GLFW/glfw3.h>

namespace {

constexpr float kEnvLodStep = 0.5f;

bool is_shift_down(GLFWwindow* window) {
    return glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
}

bool is_key_pressed_once(GLFWwindow* window, int key, bool* wasDown) {
    const bool isDown = glfwGetKey(window, key) == GLFW_PRESS;
    const bool pressedOnce = isDown && !(*wasDown);
    *wasDown = isDown;
    return pressedOnce;
}

const char* kPbrDebugModeNames[] = {
    "Full PBR (Diffuse-only)", "Albedo", "Normal", "Metallic", "Roughness", "AO", "Irradiance (Diff)", "Prefilter (Spec)", "BRDF LUT", "1-Bounce GI (Probes)"};

void set_ibl_debug_mode(VulkanEngine* engine, int mode) {
    constexpr int kMaxMode = 9; // Synchronized with names array
    if (mode < 0) {
        mode = 0;
    } else if (mode > kMaxMode) {
        mode = kMaxMode;
    }
    if (engine->iblDebugMode != mode) {
        engine->iblDebugMode = mode;
        LOG_INFO("runtime", "PBR Debug Mode: %s", kPbrDebugModeNames[engine->iblDebugMode]);
    }
}

void handle_camera_and_envmap_toggles(VulkanEngine* engine) {
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

    // Toggle Billboard Mode
    bool billboardDown = glfwGetKey(engine->window, GLFW_KEY_B) == GLFW_PRESS;
    if (billboardDown && !engine->billboardKeyWasDown) {
        engine->billboardMode = !engine->billboardMode;
        LOG_INFO("input", "Billboard mode: %s", engine->billboardMode ? "ON" : "OFF");
    }
    engine->billboardKeyWasDown = billboardDown;

    // Toggle Wireframe Mode (Legacy OGL uses 'Z')
    bool wireframeDown = glfwGetKey(engine->window, GLFW_KEY_Z) == GLFW_PRESS;
    if (wireframeDown && !engine->wireframeKeyWasDown) {
        engine->wireframeMode = !engine->wireframeMode;
        LOG_INFO("input", "Wireframe mode: %s", engine->wireframeMode ? "ON" : "OFF");
    }
    engine->wireframeKeyWasDown = wireframeDown;
}

void handle_env_navigation(VulkanEngine* engine, bool shiftDown) {
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

void handle_ibl_debug_inputs(VulkanEngine* engine) {
    for (int digit = 0; digit <= 9; ++digit) {
        const int key = GLFW_KEY_0 + digit;
        if (is_key_pressed_once(engine->window, key, &engine->iblDebugDigitKeyWasDown[digit])) {
            set_ibl_debug_mode(engine, digit);
        }
    }

    if (is_key_pressed_once(engine->window, GLFW_KEY_LEFT_BRACKET, &engine->iblDebugPrevKeyWasDown)) {
        set_ibl_debug_mode(engine, engine->iblDebugMode - 1);
    }
    if (is_key_pressed_once(engine->window, GLFW_KEY_RIGHT_BRACKET, &engine->iblDebugNextKeyWasDown)) {
        set_ibl_debug_mode(engine, engine->iblDebugMode + 1);
    }

    if (is_key_pressed_once(engine->window, GLFW_KEY_F5, &engine->iblDebugF5KeyWasDown)) {
        set_ibl_debug_mode(engine, (engine->iblDebugMode + 1) % 10);
    }

    if (is_key_pressed_once(engine->window, GLFW_KEY_O, &engine->iblExportKeyWasDown)) {
        vk_ibl_export_maps(engine);
        LOG_INFO("runtime", "IBL maps export requested (/tmp/ibl_tests/vk)");
    }
}

void handle_postprocess_inputs(VulkanEngine* engine) {
    if (glfwGetKey(engine->window, GLFW_KEY_KP_ADD) == GLFW_PRESS) {
        engine->exposure += (0.1f * engine->lastFrameDeltaSeconds * 2.0f); // Smoother adjustment
        LOG_INFO("runtime", "New exposure value: %.2fx", engine->exposure);
    }
    if (glfwGetKey(engine->window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS) {
        engine->exposure = std::max(0.01f, engine->exposure - (0.1f * engine->lastFrameDeltaSeconds * 2.0f));
        LOG_INFO("runtime", "New exposure value: %.2fx", engine->exposure);
    }

    if (is_key_pressed_once(engine->window, GLFW_KEY_0, &engine->postResetKeyWasDown) ||
        is_key_pressed_once(engine->window, GLFW_KEY_KP_0, &engine->postResetKeyWasDown)) {
        engine->exposure = 1.0f;
        engine->saturation = 1.0f;
        engine->contrast = 1.0f;
        engine->gamma = 1.0f;
        engine->gain = 1.0f;
        engine->offset = 0.0f;
        engine->wbTemp = 6500.0f;
        engine->wbTint = 0.0f;
        LOG_INFO("runtime", "Post-processing reset to defaults");
    }
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
    handle_camera_and_envmap_toggles(engine);

    const bool shiftDown = is_shift_down(engine->window);
    handle_env_navigation(engine, shiftDown);
    handle_ibl_debug_inputs(engine);
    handle_postprocess_inputs(engine);

    if (is_key_pressed_once(engine->window, GLFW_KEY_B, &engine->billboardKeyWasDown)) {
        engine->billboardMode = !engine->billboardMode;
        LOG_INFO("runtime", "Sphere Rendering Mode: %s", engine->billboardMode ? "BILLBOARD (Raytraced)" : "ICOSPHERE (Triangulated)");
    }
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
