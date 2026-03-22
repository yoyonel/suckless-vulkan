#ifndef TRACY_VULKAN_H
#define TRACY_VULKAN_H

#include "vk_engine.h"

#ifdef TRACY_ENABLE
#include <tracy/TracyVulkan.hpp>
#define SVK_TRACY_VK_NAMED_ZONE(varname, engine, commandBuffer, name)                                                                                          \
    TracyVkNamedZone(static_cast<TracyVkCtx>((engine)->tracyVkContext), varname, commandBuffer, name, true)
#else
#define SVK_TRACY_VK_NAMED_ZONE(varname, engine, commandBuffer, name)
#endif

bool tracy_vk_context_init(VulkanEngine* engine);
void tracy_vk_context_destroy(VulkanEngine* engine);
void tracy_vk_collect(VulkanEngine* engine, VkCommandBuffer commandBuffer);

#endif