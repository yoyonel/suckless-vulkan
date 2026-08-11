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

// Capacité Scratchpad TLS (2 MB). Mémoire jetable par frame (Descripteurs, CommandLists).
constexpr std::size_t TLS_SCRATCH_ARENA_CAPACITY_BYTES = 2ULL * 1024ULL * 1024ULL;

struct LinearArena {
    uint8_t* memory;
    std::size_t capacity;
    std::size_t offset;
};

void arena_init(LinearArena* arena, std::size_t capacity);
void* arena_alloc(LinearArena* arena, std::size_t size, std::size_t align = 16);
void arena_reset(LinearArena* arena);
void arena_free(LinearArena* arena);

struct TLS_Arena_Wrapper {
    LinearArena arena;
    TLS_Arena_Wrapper() noexcept;
    ~TLS_Arena_Wrapper() noexcept;
};

extern thread_local TLS_Arena_Wrapper tls_scratch;

struct BillboardSoA {
    int count;
    int capacity;
    glm::vec4* pos;
    int* materialIdx;
};

enum class InputState : uint8_t { Released = 0, Pressed, PressedOnce };

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

    InputState pausePressed;
    InputState resetPressed;
    InputState speedUpPressed;
    InputState speedDownPressed;

    InputState cameraTogglePressed;
    InputState showEnvmapTogglePressed;
    InputState envPageUpPressed;
    InputState envPageDownPressed;
    bool envShiftDown;

    InputState iblDebugDigitPressed[10];
    InputState iblDebugPrevPressed;
    InputState iblDebugNextPressed;
    InputState iblExportPressed;
    InputState iblDebugF6Pressed;

    InputState cameraResetPressed;
    InputState postResetPressed;
    bool postExposureAddDown;
    bool postExposureSubDown;

    InputState billboardPressed;
    InputState wireframePressed;
};

struct TimeState {
    float animationTimeSeconds{0.0f};
    float animationSpeed{1.0f};
    float lastFrameDeltaSeconds{0.0f};
    bool animationPaused{false};
};

struct WindowState {
    bool isFullscreen{false};
    int windowedPosX{0};
    int windowedPosY{0};
    int windowedWidth{1280};
    int windowedHeight{720};
};

struct InputTracker {
    bool pauseKeyWasDown{false};
    bool resetKeyWasDown{false};
    bool speedUpKeyWasDown{false};
    bool speedDownKeyWasDown{false};
    bool fullscreenKeyWasDown{false};
    bool escapeKeyWasDown{false};
    bool cameraToggleKeyWasDown{false};
    bool showEnvmapToggleKeyWasDown{false};
    bool envPageUpKeyWasDown{false};
    bool envPageDownKeyWasDown{false};
    bool iblDebugDigitKeyWasDown[10]{false};
    bool iblDebugPrevKeyWasDown{false};
    bool iblDebugNextKeyWasDown{false};
    bool iblExportKeyWasDown{false};
    bool iblDebugF6KeyWasDown{false};
    bool cameraResetKeyWasDown{false};
    bool postResetKeyWasDown{false};
    bool postExposureAddKeyWasDown{false};
    bool postExposureSubKeyWasDown{false};
    bool billboardKeyWasDown{false};
    bool wireframeKeyWasDown{false};
    bool vsyncKeyWasDown{false};
};

struct SceneData {
    LinearArena arena;
    BillboardSoA billboardSoA;
    glm::vec3* instancePositions{nullptr};
    uint32_t instanceCount{0};
};

struct RenderSettings {
    float envLod{0.0f};
    float iblIntensity{1.0f};
    float iblDebugScale{1.0f};
    float exposure{1.0f};
    float saturation{1.0f};
    float contrast{1.0f};
    float gamma{1.0f};
    float gain{1.0f};
    float offset{0.0f};
    float wbTemp{0.0f};
    float wbTint{0.0f};
    int iblDebugMode{0};
    bool billboardMode{true};
    bool wireframeMode{false};
    bool showEnvmap{true};
    bool pbrEnabled{true};
    bool iblEnabled{true};
};

struct CoreEngine {
    IRHI* rhi;

    TimeState time;
    WindowState window;
    InputTracker inputTracking;

    bool cameraEnabled;
    RenderSettings render;

    Camera camera;
    std::chrono::steady_clock::time_point lastFrameTimestamp;

    SceneData scene;

    bool vsync;
};

void core_engine_init(CoreEngine* core);
void core_engine_update(CoreEngine* core, const CoreInput* input, float maxFrameDeltaSeconds);

#endif
