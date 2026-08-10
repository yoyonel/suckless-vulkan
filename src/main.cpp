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

bool parse_arguments(int argc, char** argv, EngineState* state) {
    state->core.vsync = false;
    const char* vsyncEnv = std::getenv("SVK_VSYNC");
    if (vsyncEnv != nullptr) {
        state->core.vsync = (std::string(vsyncEnv) != "0");
    }
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--no-vsync")
            state->core.vsync = false;
        else if (arg == "--vsync")
            state->core.vsync = true;
        else if (arg == "--nullrhi")
            state->useNullRHI = true;
    }
    return true;
}

bool init_glfw(EngineState* state) {
    if (glfwInit() != GLFW_TRUE)
        return false;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    state->window = glfwCreateWindow(1024, 768, "Vulkan - Icosphere Full GPU", NULL, NULL);
    return state->window != nullptr;
}

bool load_initial_rhi(EngineState* state, std::string& libName, CreateRHIFunc& createFunc, DestroyRHIFunc& destroyFunc) {
    libName = state->useNullRHI ? "libnull_rhi.so" : "libvulkan_rhi.so";
    if (!state->rhiModule.Load(libName)) {
        LOG_CRITICAL("app", "Failed to load RHI module %s", libName.c_str());
        return false;
    }
    createFunc = (CreateRHIFunc)state->rhiModule.GetSymbol("CreateRHI");
    destroyFunc = (DestroyRHIFunc)state->rhiModule.GetSymbol("DestroyRHI");
    if (!createFunc || !destroyFunc) {
        LOG_CRITICAL("app", "Failed to find CreateRHI/DestroyRHI symbols");
        return false;
    }
    state->rhi = createFunc(state);
    if (!state->rhi->Init()) {
        LOG_CRITICAL("app", "Echec de l'initialisation.");
        return false;
    }
    return true;
}

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

int main(int argc, char** argv) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset); // Lock to P-Core 0
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif

    EngineState state = {};
    parse_arguments(argc, argv, &state);

    if (!tracy_client_startup("suckless-vulkan")) {
        LOG_CRITICAL("tracy", "Impossible d'initialiser le client Tracy pour cette session.");
        return -1;
    }

    arena_init(&state.rhiArena, RHI_ARENA_CAPACITY_BYTES);
    core_engine_init(&state.core);

    if (!init_glfw(&state)) {
        return -1;
    }

    std::string libName;
    CreateRHIFunc createFunc = nullptr;
    DestroyRHIFunc destroyFunc = nullptr;
    if (!load_initial_rhi(&state, libName, createFunc, destroyFunc)) {
        tracy_client_shutdown();
        return -1;
    }

    LOG_INFO("app", "Vulkan initialise avec succes ! La fenetre devrait apparaitre.");

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

    arena_free(&state.rhiArena);
    arena_free(&state.core.scene.arena);

    tracy_client_shutdown();
    return 0;
}
