#include "tracy_vulkan.h"

#ifdef TRACY_ENABLE

#include "app_log.h"

GfxResult tracy_vk_context_init(VulkanEngine* engine) {
    if (engine == nullptr || engine->ctx.physicalDevice == VK_NULL_HANDLE || engine->ctx.device == VK_NULL_HANDLE ||
        engine->ctx.graphicsQueue == VK_NULL_HANDLE || engine->commandBuffer == VK_NULL_HANDLE) {
        LOG_ERROR("tracy", "Impossible d'initialiser le contexte Tracy Vulkan: handles manquants.");
        return GfxResult::ErrorInitializationFailed;
    }

    if (engine->tracyVkContext != nullptr) {
        return GfxResult::Success;
    }

    auto gpdctd = reinterpret_cast<PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT>(
        vkGetInstanceProcAddr(engine->ctx.instance, "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT"));
    if (gpdctd == nullptr) {
        gpdctd = reinterpret_cast<PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT>(
            vkGetInstanceProcAddr(engine->ctx.instance, "vkGetPhysicalDeviceCalibrateableTimeDomainsKHR"));
    }

    auto gct = reinterpret_cast<PFN_vkGetCalibratedTimestampsEXT>(vkGetDeviceProcAddr(engine->ctx.device, "vkGetCalibratedTimestampsEXT"));
    if (gct == nullptr) {
        gct = reinterpret_cast<PFN_vkGetCalibratedTimestampsEXT>(vkGetDeviceProcAddr(engine->ctx.device, "vkGetCalibratedTimestampsKHR"));
    }

    auto qpreset = reinterpret_cast<PFN_vkResetQueryPoolEXT>(vkGetDeviceProcAddr(engine->ctx.device, "vkResetQueryPool"));
    if (qpreset == nullptr) {
        qpreset = reinterpret_cast<PFN_vkResetQueryPoolEXT>(vkGetDeviceProcAddr(engine->ctx.device, "vkResetQueryPoolEXT"));
    }

    if (qpreset != nullptr && gpdctd != nullptr && gct != nullptr) {
        engine->tracyVkContext = TracyVkContextHostCalibrated(engine->ctx.physicalDevice, engine->ctx.device, qpreset, gpdctd, gct);
        LOG_INFO("tracy", "Contexte Tracy Vulkan Host-Calibre (hostQueryReset + VK_EXT_calibrated_timestamps) initialise.");
    } else if (gpdctd != nullptr && gct != nullptr) {
        engine->tracyVkContext =
            TracyVkContextCalibrated(engine->ctx.physicalDevice, engine->ctx.device, engine->ctx.graphicsQueue, engine->commandBuffer, gpdctd, gct);
        LOG_INFO("tracy", "Contexte Tracy Vulkan calibre (VK_EXT_calibrated_timestamps) initialise.");
    } else {
        engine->tracyVkContext = TracyVkContext(engine->ctx.physicalDevice, engine->ctx.device, engine->ctx.graphicsQueue, engine->commandBuffer);
        LOG_INFO("tracy", "Contexte Tracy Vulkan standard initialise.");
    }

    if (engine->tracyVkContext == nullptr) {
        LOG_ERROR("tracy", "Echec de creation du contexte Tracy Vulkan.");
        return GfxResult::ErrorInitializationFailed;
    }

    TracyVkContextName(static_cast<TracyVkCtx>(engine->tracyVkContext), "Vulkan Graphics Queue", 21);
    return GfxResult::Success;
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
    if (engine == nullptr || engine->tracyVkContext == nullptr) {
        return;
    }

    auto ctx = static_cast<TracyVkCtx>(engine->tracyVkContext);
    if (commandBuffer != VK_NULL_HANDLE) {
        TracyVkCollect(ctx, commandBuffer);
    } else {
        TracyVkCollectHost(ctx);
    }
}

#else

GfxResult tracy_vk_context_init(VulkanEngine* engine) {
    (void)engine;
    return GfxResult::Success;
}

void tracy_vk_context_destroy(VulkanEngine* engine) {
    (void)engine;
}

void tracy_vk_collect(VulkanEngine* engine, VkCommandBuffer commandBuffer) {
    (void)engine;
    (void)commandBuffer;
}

#endif