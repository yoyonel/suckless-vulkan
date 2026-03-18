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
    LOG_INFO("app",
             "Controles: P pause/reprise, SPACE reset camera, Fleche haut accelere, Fleche bas ralentit, R reinitialise anime, "
             "C camera souris on/off, W/A/S/D/Q/E deplacement, molette impulse cinétique, K skybox on/off, PageUp/PageDown HDR, Shift+PageUp/PageDown LOD, F11 "
             "fullscreen/fenetre, ESC quitter, 0..9 modes debug IBL (F5 cycle), [/] mode precedent/suivant, O export maps IBL.");

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