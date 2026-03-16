#include "vk_engine.h"
#include <stdio.h>

int main() {
    VulkanEngine engine = {};

    printf("Initialisation de Vulkan...\n");
    if (!init_vulkan_engine(&engine)) {
        printf("Echec de l'initialisation.\n");
        return -1;
    }
    printf("Vulkan initialise avec succes ! La fenetre devrait apparaitre.\n");

    // main  loop
    while (!glfwWindowShouldClose(engine.window)) {
        glfwPollEvents();
        if (!draw_frame(&engine)) {
            fprintf(stderr, "Echec du rendu d'une frame.\n");
            cleanup_vulkan_engine(&engine);
            return 1;
        }
    }

    printf("Nettoyage et fermeture...\n");
    cleanup_vulkan_engine(&engine);
    return 0;
}