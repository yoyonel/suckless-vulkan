#ifndef VK_ENGINE_FRAME_H
#define VK_ENGINE_FRAME_H

#include "vk_engine.h"

using RecreateSwapchainFn = GfxResult (*)(VulkanEngine*);

void vk_update_animation_clock(VulkanEngine* engine, float maxFrameDeltaSeconds);
GfxResult vk_draw_frame_internal(VulkanEngine* engine, RecreateSwapchainFn recreateSwapchain);

#endif
