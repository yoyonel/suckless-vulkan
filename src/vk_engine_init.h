#ifndef VK_ENGINE_INIT_H
#define VK_ENGINE_INIT_H

#include "vk_engine.h"

bool vk_init_vulkan_engine(VulkanEngine* engine);
void vk_cleanup_vulkan_engine(VulkanEngine* engine);
bool vk_recreate_swapchain(VulkanEngine* engine);

#endif
