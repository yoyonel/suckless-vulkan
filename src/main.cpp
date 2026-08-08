#include "app_log.h"
#include "engine_state.h"
#include "runtime_controls.h"
#include "tracy_client.h"

typedef IRHI* (*CreateRHIFunc)(EngineState*);
typedef void (*DestroyRHIFunc)(IRHI*);

bool handle_rhi_reload(EngineState* state, const std::string& libName, CreateRHIFunc& createFunc, DestroyRHIFunc& destroyFunc) {
    LOG_INFO("app", "F5 pressed: Reloading RHI module...");

    state->rhi->Shutdown();
    destroyFunc(state->rhi);
    state->rhiModule.Unload();

    if (!state->rhiModule.Load(libName)) {
        LOG_CRITICAL("app", "Failed to reload RHI module %s", libName.c_str());
        return false;
    }
    createFunc = (CreateRHIFunc)state->rhiModule.GetSymbol("CreateRHI");
    destroyFunc = (DestroyRHIFunc)state->rhiModule.GetSymbol("DestroyRHI");
    if (!createFunc || !destroyFunc) {
        LOG_CRITICAL("app", "Failed to find symbols in reloaded RHI module");
        return false;
    }

    state->rhi = createFunc(state);
    if (!state->rhi->Init()) {
        LOG_CRITICAL("app", "Failed to initialize reloaded RHI");
        return false;
    }
    LOG_INFO("app", "RHI module reloaded successfully!");
    return true;
}

int main(int argc, char** argv) {
    EngineState state = {};

    // 1. Initial Default (Disabled by default to match OGL behavior/preferences)
    state.core.vsync = false;

    // 2. Environment Variable Override
    const char* vsyncEnv = std::getenv("SVK_VSYNC");
    if (vsyncEnv != nullptr) {
        state.core.vsync = (std::string(vsyncEnv) != "0");
    }

    // 3. CLI Argument Override (Highest priority)
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--no-vsync") {
            state.core.vsync = false;
        } else if (arg == "--vsync") {
            state.core.vsync = true;
        } else if (arg == "--nullrhi") {
            state.useNullRHI = true;
        }
    }

    if (!tracy_client_startup("suckless-vulkan")) {
        LOG_CRITICAL("tracy", "Impossible d'initialiser le client Tracy pour cette session.");
        return -1;
    }

    if (glfwInit() != GLFW_TRUE) {
        return -1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    state.window = glfwCreateWindow(1024, 768, "Vulkan - Icosphere Full GPU", NULL, NULL);
    if (!state.window) {
        return -1;
    }

    std::string libName = state.useNullRHI ? "libnull_rhi.so" : "libvulkan_rhi.so";
    if (!state.rhiModule.Load(libName)) {
        LOG_CRITICAL("app", "Failed to load RHI module %s", libName.c_str());
        return -1;
    }

    CreateRHIFunc createFunc = (CreateRHIFunc)state.rhiModule.GetSymbol("CreateRHI");
    DestroyRHIFunc destroyFunc = (DestroyRHIFunc)state.rhiModule.GetSymbol("DestroyRHI");
    if (!createFunc || !destroyFunc) {
        LOG_CRITICAL("app", "Failed to find CreateRHI/DestroyRHI symbols");
        return -1;
    }

    state.rhi = createFunc(&state);
    if (!state.rhi->Init()) {
        LOG_CRITICAL("app", "Echec de l'initialisation.");
        tracy_client_shutdown();
        return -1;
    }

    LOG_INFO("app", "Vulkan initialise avec succes ! La fenetre devrait apparaitre.");

    // main  loop
    bool f5_was_down = false;
    while (!glfwWindowShouldClose(state.window)) {
        glfwPollEvents();

        bool is_f5_down = (glfwGetKey(state.window, GLFW_KEY_F5) == GLFW_PRESS);
        if (is_f5_down && !f5_was_down) {
            if (!handle_rhi_reload(&state, libName, createFunc, destroyFunc)) {
                break;
            }
        }
        f5_was_down = is_f5_down;

        runtime_update_controls(&state, runtime_default_window_ops());
        tracy_client_poll_connection();
        state.rhi->HandleInputs(runtime_default_window_ops());
        if (!state.rhi->DrawFrame()) {
            LOG_ERROR("app", "Echec du rendu d'une frame.");
            break;
        }
        tracy_client_mark_frame();
    }

    LOG_INFO("app", "Nettoyage et fermeture...");

    state.rhi->Shutdown();
    if (destroyFunc)
        destroyFunc(state.rhi);

    state.rhiModule.Unload();
    glfwDestroyWindow(state.window);
    glfwTerminate();
    tracy_client_shutdown();
    return 0;
}