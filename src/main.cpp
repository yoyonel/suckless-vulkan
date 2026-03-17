#include "app_log.h"
#include "runtime_controls.h"
#include "vk_engine.h"

int main() {
    VulkanEngine engine = {};

    LOG_INFO("app", "Initialisation de Vulkan...");
    if (!init_vulkan_engine(&engine)) {
        LOG_CRITICAL("app", "Echec de l'initialisation.");
        return -1;
    }
    LOG_INFO("app", "Vulkan initialise avec succes ! La fenetre devrait apparaitre.");
    LOG_INFO("app", "Controles: Espace pause/reprise, Fleche haut accelere, Fleche bas ralentit, R reinitialise, F11 fullscreen/fenetre, ESC quitter.");

    // main  loop
    while (!glfwWindowShouldClose(engine.window)) {
        glfwPollEvents();
        runtime_update_controls(&engine, runtime_default_window_ops());
        if (!draw_frame(&engine)) {
            LOG_ERROR("app", "Echec du rendu d'une frame.");
            cleanup_vulkan_engine(&engine);
            return 1;
        }
    }

    LOG_INFO("app", "Nettoyage et fermeture...");
    cleanup_vulkan_engine(&engine);
    return 0;
}