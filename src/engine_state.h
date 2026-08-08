#ifndef ENGINE_STATE_H
#define ENGINE_STATE_H

#include "core_engine.h"
#include "module_loader.h"

struct GLFWwindow;
class IRHI;

struct EngineState {
    bool useNullRHI{false};
    GLFWwindow* window{nullptr};
    CoreInput currentInput{};
    CoreEngine core{};
    ModuleLoader rhiModule;
    IRHI* rhi{nullptr};
};

#endif
