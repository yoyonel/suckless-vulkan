#ifndef CORE_ENGINE_H
#define CORE_ENGINE_H

#include "camera.h"
#include "rhi/rhi.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

// --- CONSTANTES MÉMOIRE ---
// Capacité max (32 MB). Fixe : 1M particules * 16 octets (SoA) + padding.
constexpr std::size_t CORE_ARENA_CAPACITY_BYTES = 32ULL * 1024ULL * 1024ULL;

struct LinearArena {
    uint8_t* memory;
    std::size_t capacity;
    std::size_t offset;
};

void arena_init(LinearArena* arena, std::size_t capacity);
void* arena_alloc(LinearArena* arena, std::size_t size, std::size_t align = 16);
void arena_free(LinearArena* arena);

struct BillboardSoA {
    int count;
    int capacity;
    glm::vec4* pos;
    int* materialIdx;
};

struct CoreInput {
    bool moveForward;
    bool moveBackward;
    bool moveLeft;
    bool moveRight;
    bool moveUp;
    bool moveDown;

    float mouseDeltaX;
    float mouseDeltaY;
    float scrollDelta;

    bool pausePressed;
    bool resetPressed;
    bool speedUpPressed;
    bool speedDownPressed;

    bool cameraTogglePressed;
    bool showEnvmapTogglePressed;
    bool envPageUpPressed;
    bool envPageDownPressed;
    bool envShiftDown;

    bool iblDebugDigitPressed[10];
    bool iblDebugPrevPressed;
    bool iblDebugNextPressed;
    bool iblExportPressed;
    bool iblDebugF6Pressed;

    bool cameraResetPressed;
    bool postResetPressed;
    bool postExposureAddDown;
    bool postExposureSubDown;

    bool billboardPressed;
    bool wireframePressed;
};

struct CoreEngine {
    IRHI* rhi;

    float animationTimeSeconds;
    float animationSpeed;
    bool animationPaused;
    bool pauseKeyWasDown;
    bool resetKeyWasDown;
    bool speedUpKeyWasDown;
    bool speedDownKeyWasDown;
    bool fullscreenKeyWasDown;
    bool escapeKeyWasDown;
    bool isFullscreen;
    bool cameraToggleKeyWasDown;
    bool showEnvmapToggleKeyWasDown;
    bool envPageUpKeyWasDown;
    bool envPageDownKeyWasDown;
    bool cameraEnabled;
    bool showEnvmap;
    float envLod;
    int iblDebugMode;
    float iblDebugScale;
    bool iblDebugDigitKeyWasDown[10];
    bool iblDebugPrevKeyWasDown;
    bool iblDebugNextKeyWasDown;
    bool iblExportKeyWasDown;
    bool iblDebugF6KeyWasDown;
    bool cameraResetKeyWasDown;
    bool postResetKeyWasDown;
    bool postExposureAddKeyWasDown;
    bool postExposureSubKeyWasDown;
    int windowedPosX;
    int windowedPosY;
    int windowedWidth;
    int windowedHeight;
    float lastFrameDeltaSeconds;

    bool billboardMode;
    bool billboardKeyWasDown;
    bool wireframeMode;
    bool wireframeKeyWasDown;

    Camera camera;
    std::chrono::steady_clock::time_point lastFrameTimestamp;

    LinearArena arena;
    BillboardSoA billboardSoA;

    glm::vec3* instancePositions;
    uint32_t instanceCount;

    bool pbrEnabled;
    bool iblEnabled;
    float iblIntensity;

    float exposure;
    float saturation;
    float contrast;
    float gamma;
    float gain;
    float offset;
    float wbTemp;
    float wbTint;

    bool vsync;
    bool vsyncKeyWasDown;
};

void core_engine_init(CoreEngine* core);
void core_engine_update(CoreEngine* core, const CoreInput* input, float maxFrameDeltaSeconds);

#endif
