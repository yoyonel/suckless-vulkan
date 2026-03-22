#include "tracy_vulkan.h"

#ifdef TRACY_ENABLE

#include "app_log.h"

bool tracy_vk_context_init(VulkanEngine* engine) {
    if (engine == nullptr || engine->physicalDevice == VK_NULL_HANDLE || engine->device == VK_NULL_HANDLE || engine->graphicsQueue == VK_NULL_HANDLE ||
        engine->commandBuffer == VK_NULL_HANDLE) {
        LOG_ERROR("tracy", "Impossible d'initialiser le contexte Tracy Vulkan: handles manquants.");
        return false;
    }

    if (engine->tracyVkContext != nullptr) {
        return true;
    }

    engine->tracyVkContext = TracyVkContext(engine->physicalDevice, engine->device, engine->graphicsQueue, engine->commandBuffer);
    if (engine->tracyVkContext == nullptr) {
        LOG_ERROR("tracy", "Echec de creation du contexte Tracy Vulkan.");
        return false;
    }

    TracyVkContextName(static_cast<TracyVkCtx>(engine->tracyVkContext), "Vulkan Graphics Queue", 20);
    LOG_INFO("tracy", "Contexte Tracy Vulkan initialise.");
    return true;
}

void tracy_vk_context_destroy(VulkanEngine* engine) {
    if (engine == nullptr || engine->tracyVkContext == nullptr) {
        return;
    }

    TracyVkDestroy(static_cast<TracyVkCtx>(engine->tracyVkContext));
    engine->tracyVkContext = nullptr;
    LOG_INFO("tracy", "Contexte Tracy Vulkan detruit.");
}

void tracy_vk_collect(VulkanEngine* engine, VkCommandBuffer commandBuffer) {
    if (engine == nullptr || engine->tracyVkContext == nullptr || commandBuffer == VK_NULL_HANDLE) {
        return;
    }

    TracyVkCollect(static_cast<TracyVkCtx>(engine->tracyVkContext), commandBuffer);
}

#else

bool tracy_vk_context_init(VulkanEngine* engine) {
    (void)engine;
    return true;
}

void tracy_vk_context_destroy(VulkanEngine* engine) {
    (void)engine;
}

void tracy_vk_collect(VulkanEngine* engine, VkCommandBuffer commandBuffer) {
    (void)engine;
    (void)commandBuffer;
}

#endif