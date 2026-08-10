#ifndef VK_ENGINE_ENVMAP_H
#define VK_ENGINE_ENVMAP_H

#include "vk_engine.h"

GfxResult vk_init_environment_catalog(VulkanEngine* engine);
GfxResult vk_init_environment_texture(VulkanEngine* engine);
void vk_cleanup_environment_resources(VulkanEngine* engine);

GfxResult vk_start_hdr_io_thread(VulkanEngine* engine);
void vk_stop_hdr_io_thread(VulkanEngine* engine);
void vk_process_ready_environment_texture(VulkanEngine* engine);

void vk_switch_environment_texture(VulkanEngine* engine, int direction);
void vk_adjust_env_lod(VulkanEngine* engine, float delta);

#endif
