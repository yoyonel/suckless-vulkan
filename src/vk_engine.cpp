#include "vk_engine.h"
#include "vk_engine_frame.h"
#include "vk_engine_init.h"

// --- HELPERS DEBUG (RenderDoc) ---

void vk_set_object_name(VkDevice device, uint64_t handle, VkObjectType type, const char* name) {
    VkDebugUtilsObjectNameInfoEXT nameInfo{};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = type;
    nameInfo.objectHandle = handle;
    nameInfo.pObjectName = name;
    auto func = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
    if (func)
        func(device, &nameInfo);
}

void vk_begin_label(VkDevice device, VkCommandBuffer cb, const char* name, float r, float g, float b) {
    VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, nullptr, name, {r, g, b, 1.0f}};
    auto f = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetDeviceProcAddr(device, "vkCmdBeginDebugUtilsLabelEXT");
    if (f)
        f(cb, &label);
}

void vk_end_label(VkDevice device, VkCommandBuffer cb) {
    auto f = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetDeviceProcAddr(device, "vkCmdEndDebugUtilsLabelEXT");
    if (f)
        f(cb);
}

// --- FONCTIONS PUBLIQUES ---

bool init_vulkan_engine(VulkanEngine* engine) {
    return vk_init_vulkan_engine(engine);
}

bool draw_frame(VulkanEngine* engine) {
    return vk_draw_frame_internal(engine, vk_recreate_swapchain);
}

void cleanup_vulkan_engine(VulkanEngine* engine) {
    vk_cleanup_vulkan_engine(engine);
}
