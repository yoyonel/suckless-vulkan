#ifndef SWAPCHAIN_INIT_H
#define SWAPCHAIN_INIT_H

#include "engine_config.h"
#include "result.h"
#include "rhi/rhi_ptr.h"
#include <vulkan/vulkan.h>

struct VulkanEngine;

struct SwapchainManager {
    VkSwapchainKHR swapchain{VK_NULL_HANDLE};
    VkFormat swapchainImageFormat{VK_FORMAT_UNDEFINED};
    VkExtent2D swapchainExtent{0, 0};
    uint32_t imageCount{0};

    VkImage swapchainImages[config::kMaxSwapchainImages];
    VkImageView swapchainImageViews[config::kMaxSwapchainImages];
    VkFramebuffer swapchainFramebuffers[config::kMaxSwapchainImages];

    VkFormat depthFormat{VK_FORMAT_UNDEFINED};
    rhi::TexturePtr depthImage;
    rhi::TexturePtr colorAttachment;
    VkFramebuffer colorFramebuffer{VK_NULL_HANDLE};

    GfxResult init(VulkanEngine* engine);
    void cleanup_targets(VulkanEngine* engine);
    void cleanup_dependent_resources(VulkanEngine* engine);
};

GfxResult device_supports_swapchain(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);

#endif // SWAPCHAIN_INIT_H
