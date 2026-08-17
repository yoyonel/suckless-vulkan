#include "core_engine.h"
#include "app_log.h"
#include "tracy_client.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>

void arena_init(LinearArena* arena, std::size_t capacity) {
    arena->memory = static_cast<uint8_t*>(std::malloc(capacity));
    SVK_TRACY_ALLOC(arena->memory, capacity);
    arena->capacity = capacity;
    arena->offset = 0;
}

void* arena_alloc(LinearArena* arena, std::size_t size, std::size_t align) {
    std::size_t current = reinterpret_cast<std::size_t>(arena->memory + arena->offset);
    std::size_t aligned = (current + align - 1) & ~(align - 1);
    std::size_t padding = aligned - current;

    if (arena->offset + padding + size > arena->capacity) {
        LOG_ERROR("memory", "FATAL OOM: LinearArena capacity exceeded! (Capacity: %zu bytes, Requested offset: %zu)", arena->capacity,
                  arena->offset + padding + size);
        std::abort();
        return nullptr;
    }

    arena->offset += padding + size;
    return arena->memory + arena->offset - size;
}

void arena_reset(LinearArena* arena) {
    arena->offset = 0;
}

void arena_free(LinearArena* arena) {
    SVK_TRACY_FREE(arena->memory);
    std::free(arena->memory);
    arena->memory = nullptr;
    arena->capacity = 0;
    arena->offset = 0;
}

TLS_Arena_Wrapper::TLS_Arena_Wrapper() noexcept {
    arena_init(&arena, TLS_SCRATCH_ARENA_CAPACITY_BYTES);
}

TLS_Arena_Wrapper::~TLS_Arena_Wrapper() noexcept {
    arena_free(&arena);
}

thread_local TLS_Arena_Wrapper tls_scratch;

void core_engine_init(CoreEngine* core) {
    core->time.animationTimeSeconds = 0.0f;
    core->time.animationSpeed = 1.0f;
    core->time.animationPaused = false;
    core->inputTracking.pauseKeyWasDown = false;
    core->inputTracking.resetKeyWasDown = false;
    core->inputTracking.speedUpKeyWasDown = false;
    core->inputTracking.speedDownKeyWasDown = false;
    core->inputTracking.fullscreenKeyWasDown = false;
    core->inputTracking.escapeKeyWasDown = false;
    core->window.isFullscreen = false;
    core->inputTracking.cameraToggleKeyWasDown = false;
    core->inputTracking.showEnvmapToggleKeyWasDown = false;
    core->inputTracking.envPageUpKeyWasDown = false;
    core->inputTracking.envPageDownKeyWasDown = false;
    core->cameraEnabled = true;
    core->render.showEnvmap = true;
    core->render.envLod = 0.0f;
    core->render.iblDebugMode = 0;
    core->render.iblDebugScale = 1.0f;
    core->render.iblIntensity = 1.0f;

    for (int i = 0; i < 10; ++i) {
        core->inputTracking.iblDebugDigitKeyWasDown[i] = false;
    }

    core->inputTracking.iblDebugPrevKeyWasDown = false;
    core->inputTracking.iblDebugNextKeyWasDown = false;
    core->inputTracking.iblExportKeyWasDown = false;
    core->inputTracking.iblDebugF6KeyWasDown = false;
    core->inputTracking.cameraResetKeyWasDown = false;
    core->inputTracking.postResetKeyWasDown = false;
    core->inputTracking.postExposureAddKeyWasDown = false;
    core->inputTracking.postExposureSubKeyWasDown = false;

    core->render.billboardMode = true;
    core->inputTracking.billboardKeyWasDown = false;
    core->render.wireframeMode = false;
    core->inputTracking.wireframeKeyWasDown = false;
    core->inputTracking.bloomKeyWasDown = false;

    core->render.exposure = 1.0f;
    core->render.saturation = 1.0f;
    core->render.contrast = 1.0f;
    core->render.gamma = 1.0f;
    core->render.gain = 1.0f;
    core->render.offset = 0.0f;
    core->render.wbTemp = 6500.0f;
    core->render.wbTint = 0.0f;

    core->time.lastFrameDeltaSeconds = 0.0f;

    camera_init(&core->camera);
    core->lastFrameTimestamp = std::chrono::steady_clock::now();

    arena_init(&core->scene.arena, CORE_ARENA_CAPACITY_BYTES);
    core->scene.billboardSoA.count = 0;
    core->scene.billboardSoA.capacity = 0;
    core->scene.billboardSoA.pos = nullptr;
    core->scene.billboardSoA.materialIdx = nullptr;
}

static void process_animation_inputs(CoreEngine* core, const CoreInput* input) {
    if (input->pausePressed == InputState::PressedOnce)
        core->time.animationPaused = !core->time.animationPaused;
    if (input->resetPressed == InputState::PressedOnce) {
        core->time.animationTimeSeconds = 0.0f;
        core->time.animationSpeed = 1.0f;
    }
    if (input->speedUpPressed == InputState::PressedOnce)
        core->time.animationSpeed *= 1.25f;
    if (input->speedDownPressed == InputState::PressedOnce) {
        core->time.animationSpeed *= 0.8f;
        core->time.animationSpeed = std::max(core->time.animationSpeed, 0.1f);
    }
}

static void process_toggles_and_env_inputs(CoreEngine* core, const CoreInput* input) {
    if (input->cameraTogglePressed == InputState::PressedOnce) {
        core->cameraEnabled = !core->cameraEnabled;
        core->camera.firstMouse = true;
    }
    if (input->showEnvmapTogglePressed == InputState::PressedOnce)
        core->render.showEnvmap = !core->render.showEnvmap;
    if (input->billboardPressed == InputState::PressedOnce)
        core->render.billboardMode = !core->render.billboardMode;
    if (input->wireframePressed == InputState::PressedOnce)
        core->render.wireframeMode = !core->render.wireframeMode;
    if (input->bloomTogglePressed == InputState::PressedOnce) {
        core->render.bloomEnabled = !core->render.bloomEnabled;
        LOG_INFO("bloom", "Bloom: %s (intensity=%.2f, threshold=%.2f)", core->render.bloomEnabled ? "ENABLED" : "DISABLED", core->render.bloomIntensity,
                 core->render.bloomThreshold);
    }
    if (input->bloomDebugCyclePressed == InputState::PressedOnce) {
        core->render.bloomDebugMode = (core->render.bloomDebugMode + 1) % 5;
        const char* modeNames[] = {"OFF", "Final Map", "Prefilter", "Downsample", "Upsample"};
        LOG_INFO("bloom", "Bloom Debug: %s | Mip: %d", modeNames[core->render.bloomDebugMode], core->render.bloomDebugMip);
    }
    if (input->bloomMipCyclePressed == InputState::PressedOnce) {
        core->render.bloomDebugMip = (core->render.bloomDebugMip + 1) % 5;
        const char* modeNames[] = {"OFF", "Final Map", "Prefilter", "Downsample", "Upsample"};
        LOG_INFO("bloom", "Bloom Debug: %s | Mip: %d", modeNames[core->render.bloomDebugMode], core->render.bloomDebugMip);
    }
    if (input->bloomIntensityIncPressed == InputState::PressedOnce) {
        core->render.bloomIntensity = std::min(5.0f, core->render.bloomIntensity + 0.05f);
        LOG_INFO("bloom", "Bloom Intensity: %.2f", core->render.bloomIntensity);
    }
    if (input->bloomIntensityDecPressed == InputState::PressedOnce) {
        core->render.bloomIntensity = std::max(0.0f, core->render.bloomIntensity - 0.05f);
        LOG_INFO("bloom", "Bloom Intensity: %.2f", core->render.bloomIntensity);
    }
    if (input->bloomThresholdIncPressed == InputState::PressedOnce) {
        core->render.bloomThreshold = std::min(10.0f, core->render.bloomThreshold + 0.1f);
        LOG_INFO("bloom", "Bloom Threshold: %.2f", core->render.bloomThreshold);
    }
    if (input->bloomThresholdDecPressed == InputState::PressedOnce) {
        core->render.bloomThreshold = std::max(0.0f, core->render.bloomThreshold - 0.1f);
        LOG_INFO("bloom", "Bloom Threshold: %.2f", core->render.bloomThreshold);
    }
    if (input->autoExposureTogglePressed == InputState::PressedOnce) {
        core->render.autoExposureEnabled = !core->render.autoExposureEnabled;
        LOG_INFO("autoexposure", "Auto-Exposure: %s", core->render.autoExposureEnabled ? "ENABLED" : "DISABLED");
    }
    if (input->autoExposureDebugTogglePressed == InputState::PressedOnce) {
        core->render.autoExposureDebug = !core->render.autoExposureDebug;
        LOG_INFO("autoexposure", "Auto-Exposure Debug Overlay: %s", core->render.autoExposureDebug ? "ENABLED" : "DISABLED");
    }

    constexpr float kEnvLodStep = 0.5f;
    if (input->envPageUpPressed == InputState::PressedOnce && input->envShiftDown) {
        core->render.envLod += kEnvLodStep;
    }
    if (input->envPageDownPressed == InputState::PressedOnce && input->envShiftDown) {
        core->render.envLod = std::max(0.0f, core->render.envLod - kEnvLodStep);
    }
}

static void process_ibl_inputs(CoreEngine* core, const CoreInput* input) {
    for (int i = 0; i < 10; ++i) {
        if (input->iblDebugDigitPressed[i] == InputState::PressedOnce)
            core->render.iblDebugMode = i;
    }
    if (input->iblDebugPrevPressed == InputState::PressedOnce)
        core->render.iblDebugMode = std::max(0, core->render.iblDebugMode - 1);
    if (input->iblDebugNextPressed == InputState::PressedOnce)
        core->render.iblDebugMode = std::min(9, core->render.iblDebugMode + 1);
    if (input->iblDebugF6Pressed == InputState::PressedOnce)
        core->render.iblDebugMode = (core->render.iblDebugMode + 1) % 10;
}

static void process_camera_inputs(CoreEngine* core, const CoreInput* input) {
    if (input->cameraResetPressed == InputState::PressedOnce)
        camera_init(&core->camera);

    if (core->cameraEnabled) {
        core->camera.moveForward = input->moveForward;
        core->camera.moveBackward = input->moveBackward;
        core->camera.moveLeft = input->moveLeft;
        core->camera.moveRight = input->moveRight;
        core->camera.moveUp = input->moveUp;
        core->camera.moveDown = input->moveDown;

        if (input->mouseDeltaX != 0.0f || input->mouseDeltaY != 0.0f) {
            camera_process_mouse(&core->camera, input->mouseDeltaX, input->mouseDeltaY);
        }
        if (input->scrollDelta != 0.0f) {
            camera_process_scroll(&core->camera, input->scrollDelta);
        }
    }
}

static void process_postprocess_inputs(CoreEngine* core, const CoreInput* input, float deltaSeconds) {
    if (input->postExposureAddDown) {
        core->render.exposure += (0.1f * deltaSeconds * 2.0f);
    }
    if (input->postExposureSubDown) {
        core->render.exposure = std::max(0.01f, core->render.exposure - (0.1f * deltaSeconds * 2.0f));
    }
    if (input->postResetPressed == InputState::PressedOnce) {
        core->render.exposure = 1.0f;
        core->render.saturation = 1.0f;
        core->render.contrast = 1.0f;
        core->render.gamma = 1.0f;
        core->render.gain = 1.0f;
        core->render.offset = 0.0f;
        core->render.wbTemp = 6500.0f;
        core->render.wbTint = 0.0f;
    }
}

void core_engine_update(CoreEngine* core, const CoreInput* input, float maxFrameDeltaSeconds) {
    const auto now = std::chrono::steady_clock::now();

    if (core->lastFrameTimestamp.time_since_epoch().count() == 0) {
        core->lastFrameTimestamp = now;
        return;
    }

    float deltaSeconds = std::chrono::duration<float>(now - core->lastFrameTimestamp).count();
    core->lastFrameTimestamp = now;
    deltaSeconds = std::clamp(deltaSeconds, 0.0f, maxFrameDeltaSeconds);
    core->time.lastFrameDeltaSeconds = deltaSeconds;

    process_animation_inputs(core, input);
    process_toggles_and_env_inputs(core, input);
    process_ibl_inputs(core, input);
    process_camera_inputs(core, input);
    process_postprocess_inputs(core, input, deltaSeconds);

    // Update simulation
    if (!core->time.animationPaused) {
        core->time.animationTimeSeconds += deltaSeconds * core->time.animationSpeed;
    }

    camera_fixed_update(&core->camera, deltaSeconds);
}
