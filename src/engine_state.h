#ifndef ENGINE_STATE_H
#define ENGINE_STATE_H

#include "core_engine.h"
#include "module_loader.h"

struct GLFWwindow;
class IRHI;

constexpr std::size_t RHI_ARENA_CAPACITY_BYTES = 2ULL * 1024ULL * 1024ULL;

struct EngineState {
    bool useNullRHI{false};
    bool noFocus{false};
    GLFWwindow* window{nullptr};
    CoreInput currentInput{};
    CoreEngine core{};
    ModuleLoader rhiModule;
    IRHI* rhi{nullptr};
    LinearArena rhiArena{};
};

#endif
