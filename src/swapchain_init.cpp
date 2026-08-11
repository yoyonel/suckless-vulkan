#include "swapchain_init.h"
#include "app_log.h"
#include "rhi/vulkan_rhi.h"
#include "vk_engine.h"
#include <algorithm>

namespace {

VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& availablePresentModes, bool vsync) {
    VkPresentModeKHR selected = VK_PRESENT_MODE_FIFO_KHR;

    auto find_mode = [&](VkPresentModeKHR mode) {
        return std::any_of(availablePresentModes.begin(), availablePresentModes.end(), [mode](VkPresentModeKHR m) { return m == mode; });
    };

    if (vsync) {
        // Prefer Mailbox (Triple Buffering) for best vsync experience if available
        if (find_mode(VK_PRESENT_MODE_MAILBOX_KHR)) {
            selected = VK_PRESENT_MODE_MAILBOX_KHR;
        }
    } else {
        // Prefer Immediate for uncapped framerate
        if (find_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)) {
            selected = VK_PRESENT_MODE_IMMEDIATE_KHR;
        } else if (find_mode(VK_PRESENT_MODE_MAILBOX_KHR)) {
            selected = VK_PRESENT_MODE_MAILBOX_KHR;
        }
    }

    return selected;
}

VkExtent2D choose_swapchain_extent(GLFWwindow* window, const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
    actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

    return actualExtent;
}

VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supportedCompositeAlpha) {
    if (supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
        return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }
    if (supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
        return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    }
    if (supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) {
        return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }
    if (supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
        return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

VkFormat find_supported_format(VkPhysicalDevice physicalDevice, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        }
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

VkFormat find_depth_format(VkPhysicalDevice physicalDevice) {
    return find_supported_format(physicalDevice, {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}, VK_IMAGE_TILING_OPTIMAL,
                                 VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

} // namespace

GfxResult device_supports_swapchain(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);

    if (formatCount != 0 && presentModeCount != 0) {
        return GfxResult::Success;
    }
    return GfxResult::ErrorInitializationFailed;
}

void SwapchainManager::cleanup_targets(VulkanEngine* engine) {
    for (uint32_t i = 0; i < imageCount; i++) {
        if (swapchainFramebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(engine->device, swapchainFramebuffers[i], nullptr);
            swapchainFramebuffers[i] = VK_NULL_HANDLE;
        }
        if (swapchainImageViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(engine->device, swapchainImageViews[i], nullptr);
            swapchainImageViews[i] = VK_NULL_HANDLE;
        }
    }
}

void SwapchainManager::cleanup_dependent_resources(VulkanEngine* engine) {
    cleanup_targets(engine);

    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(engine->device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }

    depthImage.Reset();
}

GfxResult SwapchainManager::init(VulkanEngine* engine) {
    VkSurfaceCapabilitiesKHR capabilities{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(engine->physicalDevice, engine->surface, &capabilities) != VK_SUCCESS) {
        return GfxResult::ErrorInitializationFailed;
    }

    uint32_t formatCount = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(engine->physicalDevice, engine->surface, &formatCount, nullptr) != VK_SUCCESS || formatCount == 0) {
        return GfxResult::ErrorInitializationFailed;
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(engine->physicalDevice, engine->surface, &formatCount, formats.data()) != VK_SUCCESS) {
        return GfxResult::ErrorInitializationFailed;
    }

    uint32_t presentModeCount = 0;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(engine->physicalDevice, engine->surface, &presentModeCount, nullptr) != VK_SUCCESS || presentModeCount == 0) {
        return GfxResult::ErrorInitializationFailed;
    }
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(engine->physicalDevice, engine->surface, &presentModeCount, presentModes.data()) != VK_SUCCESS) {
        return GfxResult::ErrorInitializationFailed;
    }

    const VkSurfaceFormatKHR surfaceFormat = choose_surface_format(formats);
    const VkPresentModeKHR presentMode = choose_present_mode(presentModes, engine->appState->core.vsync);
    swapchainImageFormat = surfaceFormat.format;
    swapchainExtent = choose_swapchain_extent(engine->appState->window, capabilities);

    LOG_INFO("engine", "Swapchain Extent: %ux%u", swapchainExtent.width, swapchainExtent.height);

    uint32_t reqImageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && reqImageCount > capabilities.maxImageCount) {
        reqImageCount = capabilities.maxImageCount;
    }

    LOG_INFO("engine", "Swapchain: minImageCount=%u, maxImageCount=%u, using imageCount=%u", capabilities.minImageCount, capabilities.maxImageCount,
             reqImageCount);

    reqImageCount = std::min(reqImageCount, static_cast<uint32_t>(config::kMaxSwapchainImages));
    if (reqImageCount < capabilities.minImageCount) {
        return GfxResult::ErrorInitializationFailed;
    }

    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0) {
        return GfxResult::ErrorInitializationFailed;
    }

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = engine->surface;
    swapchainInfo.minImageCount = reqImageCount;
    swapchainInfo.imageFormat = swapchainImageFormat;
    swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainInfo.imageExtent = swapchainExtent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    const uint32_t queueFamilyIndices[] = {engine->graphicsQueueFamilyIndex, engine->presentQueueFamilyIndex};
    if (engine->graphicsQueueFamilyIndex != engine->presentQueueFamilyIndex) {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchainInfo.queueFamilyIndexCount = 2;
        swapchainInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = choose_composite_alpha(capabilities.supportedCompositeAlpha);
    swapchainInfo.presentMode = presentMode;
    swapchainInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(engine->device, &swapchainInfo, NULL, &swapchain) != VK_SUCCESS)
        return GfxResult::ErrorInitializationFailed;
    vk_set_object_name(engine->device, (uint64_t)swapchain, VK_OBJECT_TYPE_SWAPCHAIN_KHR, "Main_Swapchain");

    if (vkGetSwapchainImagesKHR(engine->device, swapchain, &imageCount, NULL) != VK_SUCCESS) {
        return GfxResult::ErrorInitializationFailed;
    }
    if (imageCount > config::kMaxSwapchainImages) {
        LOG_ERROR("init", "Swapchain image count (%u) exceeds MAX_SWAPCHAIN_IMAGES (%d)", imageCount, config::kMaxSwapchainImages);
        return GfxResult::ErrorInitializationFailed;
    }
    if (vkGetSwapchainImagesKHR(engine->device, swapchain, &imageCount, swapchainImages) != VK_SUCCESS) {
        return GfxResult::ErrorInitializationFailed;
    }

    for (uint32_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainImageFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(engine->device, &viewInfo, NULL, &swapchainImageViews[i]) != VK_SUCCESS) {
            return GfxResult::ErrorInitializationFailed;
        }
        const std::string swapchainViewName = "Swapchain_ImageView_" + std::to_string(i);
        vk_set_object_name(engine->device, (uint64_t)swapchainImageViews[i], VK_OBJECT_TYPE_IMAGE_VIEW, swapchainViewName.c_str());
    }

    depthFormat = find_depth_format(engine->physicalDevice);
    if (depthFormat == VK_FORMAT_UNDEFINED) {
        return GfxResult::ErrorInitializationFailed;
    }

    depthImage.Reset(engine->appState->rhi, engine->appState->rhi->CreateTexture(swapchainExtent.width, swapchainExtent.height, TextureFormat::Depth,
                                                                                 TextureUsage::DepthAttachment, 1, "Main_DepthBuffer"));

    return GfxResult::Success;
}
