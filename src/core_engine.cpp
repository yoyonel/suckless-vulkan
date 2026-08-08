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
        assert(false && "LinearArena Out Of Memory");
        std::abort();
        return nullptr;
    }

    arena->offset += padding + size;
    return arena->memory + arena->offset - size;
}

void arena_free(LinearArena* arena) {
    SVK_TRACY_FREE(arena->memory);
    std::free(arena->memory);
    arena->memory = nullptr;
    arena->capacity = 0;
    arena->offset = 0;
}

void core_engine_init(CoreEngine* core) {
    core->animationTimeSeconds = 0.0f;
    core->animationSpeed = 1.0f;
    core->animationPaused = false;
    core->pauseKeyWasDown = false;
    core->resetKeyWasDown = false;
    core->speedUpKeyWasDown = false;
    core->speedDownKeyWasDown = false;
    core->fullscreenKeyWasDown = false;
    core->escapeKeyWasDown = false;
    core->isFullscreen = false;
    core->cameraToggleKeyWasDown = false;
    core->showEnvmapToggleKeyWasDown = false;
    core->envPageUpKeyWasDown = false;
    core->envPageDownKeyWasDown = false;
    core->cameraEnabled = true;
    core->showEnvmap = true;
    core->envLod = 0.0f;
    core->iblDebugMode = 0;
    core->iblDebugScale = 1.0f;
    core->iblIntensity = 1.0f;

    for (int i = 0; i < 10; ++i) {
        core->iblDebugDigitKeyWasDown[i] = false;
    }

    core->iblDebugPrevKeyWasDown = false;
    core->iblDebugNextKeyWasDown = false;
    core->iblExportKeyWasDown = false;
    core->iblDebugF6KeyWasDown = false;
    core->cameraResetKeyWasDown = false;
    core->postResetKeyWasDown = false;
    core->postExposureAddKeyWasDown = false;
    core->postExposureSubKeyWasDown = false;

    core->billboardMode = true;
    core->billboardKeyWasDown = false;
    core->wireframeMode = false;
    core->wireframeKeyWasDown = false;

    core->exposure = 1.0f;
    core->saturation = 1.0f;
    core->contrast = 1.0f;
    core->gamma = 1.0f;
    core->gain = 1.0f;
    core->offset = 0.0f;
    core->wbTemp = 6500.0f;
    core->wbTint = 0.0f;

    core->lastFrameDeltaSeconds = 0.0f;

    camera_init(&core->camera);
    core->lastFrameTimestamp = std::chrono::steady_clock::now();

    arena_init(&core->arena, CORE_ARENA_CAPACITY_BYTES);
    core->billboardSoA.count = 0;
    core->billboardSoA.capacity = 0;
    core->billboardSoA.pos = nullptr;
    core->billboardSoA.materialIdx = nullptr;
}

static void process_animation_inputs(CoreEngine* core, const CoreInput* input) {
    if (input->pausePressed)
        core->animationPaused = !core->animationPaused;
    if (input->resetPressed) {
        core->animationTimeSeconds = 0.0f;
        core->animationSpeed = 1.0f;
    }
    if (input->speedUpPressed)
        core->animationSpeed *= 1.25f;
    if (input->speedDownPressed) {
        core->animationSpeed *= 0.8f;
        core->animationSpeed = std::max(core->animationSpeed, 0.1f);
    }
}

static void process_toggles_and_env_inputs(CoreEngine* core, const CoreInput* input) {
    if (input->cameraTogglePressed) {
        core->cameraEnabled = !core->cameraEnabled;
        core->camera.firstMouse = true;
    }
    if (input->showEnvmapTogglePressed)
        core->showEnvmap = !core->showEnvmap;
    if (input->billboardPressed)
        core->billboardMode = !core->billboardMode;
    if (input->wireframePressed)
        core->wireframeMode = !core->wireframeMode;

    constexpr float kEnvLodStep = 0.5f;
    if (input->envPageUpPressed && input->envShiftDown) {
        core->envLod += kEnvLodStep;
    }
    if (input->envPageDownPressed && input->envShiftDown) {
        core->envLod = std::max(0.0f, core->envLod - kEnvLodStep);
    }
}

static void process_ibl_inputs(CoreEngine* core, const CoreInput* input) {
    for (int i = 0; i < 10; ++i) {
        if (input->iblDebugDigitPressed[i])
            core->iblDebugMode = i;
    }
    if (input->iblDebugPrevPressed)
        core->iblDebugMode = std::max(0, core->iblDebugMode - 1);
    if (input->iblDebugNextPressed)
        core->iblDebugMode = std::min(9, core->iblDebugMode + 1);
    if (input->iblDebugF6Pressed)
        core->iblDebugMode = (core->iblDebugMode + 1) % 10;
}

static void process_camera_inputs(CoreEngine* core, const CoreInput* input) {
    if (input->cameraResetPressed)
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
        core->exposure += (0.1f * deltaSeconds * 2.0f);
    }
    if (input->postExposureSubDown) {
        core->exposure = std::max(0.01f, core->exposure - (0.1f * deltaSeconds * 2.0f));
    }
    if (input->postResetPressed) {
        core->exposure = 1.0f;
        core->saturation = 1.0f;
        core->contrast = 1.0f;
        core->gamma = 1.0f;
        core->gain = 1.0f;
        core->offset = 0.0f;
        core->wbTemp = 6500.0f;
        core->wbTint = 0.0f;
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
    core->lastFrameDeltaSeconds = deltaSeconds;

    process_animation_inputs(core, input);
    process_toggles_and_env_inputs(core, input);
    process_ibl_inputs(core, input);
    process_camera_inputs(core, input);
    process_postprocess_inputs(core, input, deltaSeconds);

    // Update simulation
    if (!core->animationPaused) {
        core->animationTimeSeconds += deltaSeconds * core->animationSpeed;
    }

    camera_fixed_update(&core->camera, deltaSeconds);
}
