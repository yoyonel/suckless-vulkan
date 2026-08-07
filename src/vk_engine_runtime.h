#ifndef VK_ENGINE_RUNTIME_H
#define VK_ENGINE_RUNTIME_H

#include "runtime_controls.h"
#include "vk_engine.h"

void vk_update_camera_key_state(VulkanEngine* engine, const WindowOps* ops);
void vk_handle_runtime_input(VulkanEngine* engine, const WindowOps* ops);
void vk_mouse_callback(GLFWwindow* window, double xpos, double ypos);
void vk_scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

#endif
