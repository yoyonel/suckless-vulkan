#include "vk_engine.h"
#include "app_log.h"
#include "icosphere.h"
#include <algorithm>
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

struct QueueFamilySelection {
    uint32_t graphicsFamily = UINT32_MAX;
    uint32_t presentFamily = UINT32_MAX;

    bool isComplete() const {
        return graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX;
    }
};

static bool has_required_device_extensions(VkPhysicalDevice physicalDevice) {
    uint32_t extensionCount = 0;
    if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr) != VK_SUCCESS) {
        return false;
    }

    std::vector<VkExtensionProperties> extensions(extensionCount);
    if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data()) != VK_SUCCESS) {
        return false;
    }

    for (const auto& extension : extensions) {
        if (strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            return true;
        }
    }

    return false;
}

static QueueFamilySelection find_queue_families(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
    QueueFamilySelection selection;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    for (uint32_t index = 0; index < queueFamilyCount; ++index) {
        const auto& queueFamily = queueFamilies[index];
        if (queueFamily.queueCount == 0) {
            continue;
        }

        if ((queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            selection.graphicsFamily = index;
        }

        VkBool32 presentSupport = VK_FALSE;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, index, surface, &presentSupport) == VK_SUCCESS && presentSupport == VK_TRUE) {
            selection.presentFamily = index;
        }

        if (selection.isComplete()) {
            break;
        }
    }

    return selection;
}

static bool device_supports_swapchain(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
    uint32_t formatCount = 0;
    uint32_t presentModeCount = 0;

    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr) != VK_SUCCESS) {
        return false;
    }
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr) != VK_SUCCESS) {
        return false;
    }

    return formatCount > 0 && presentModeCount > 0;
}

static VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& format : availableFormats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    return availableFormats[0];
}

static VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& presentMode : availablePresentModes) {
        if (presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            return presentMode;
        }
    }

    for (const auto& presentMode : availablePresentModes) {
        if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return presentMode;
        }
    }

    for (const auto& presentMode : availablePresentModes) {
        if (presentMode == VK_PRESENT_MODE_FIFO_KHR) {
            return presentMode;
        }
    }

    return availablePresentModes[0];
}

static VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supportedCompositeAlpha) {
    const VkCompositeAlphaFlagBitsKHR preferredModes[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };

    for (const auto& mode : preferredModes) {
        if ((supportedCompositeAlpha & mode) != 0) {
            return mode;
        }
    }

    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

static VkExtent2D choose_swapchain_extent(GLFWwindow* window, const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return actualExtent;
}

static VkFormat find_depth_format(VkPhysicalDevice physicalDevice) {
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };

    for (const auto& candidate : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, candidate, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            return candidate;
        }
    }

    return VK_FORMAT_UNDEFINED;
}

template <typename Handle, typename DestroyFn> static void destroy_device_handle(VkDevice device, Handle& handle, DestroyFn destroyFn) {
    if (device == VK_NULL_HANDLE || handle == VK_NULL_HANDLE) {
        return;
    }

    destroyFn(device, handle, nullptr);
    handle = VK_NULL_HANDLE;
}

static void unmap_allocation(VmaAllocator allocator, VmaAllocation allocation, void*& mappedMemory) {
    if (allocator == VK_NULL_HANDLE || allocation == VK_NULL_HANDLE || mappedMemory == nullptr) {
        return;
    }

    vmaUnmapMemory(allocator, allocation);
    mappedMemory = nullptr;
}

static void destroy_buffer_allocation(VmaAllocator allocator, VkBuffer& buffer, VmaAllocation& allocation) {
    if (allocator == VK_NULL_HANDLE || buffer == VK_NULL_HANDLE || allocation == VK_NULL_HANDLE) {
        return;
    }

    vmaDestroyBuffer(allocator, buffer, allocation);
    buffer = VK_NULL_HANDLE;
    allocation = VK_NULL_HANDLE;
}

static void destroy_image_allocation(VmaAllocator allocator, VkImage& image, VmaAllocation& allocation) {
    if (allocator == VK_NULL_HANDLE || image == VK_NULL_HANDLE || allocation == VK_NULL_HANDLE) {
        return;
    }

    vmaDestroyImage(allocator, image, allocation);
    image = VK_NULL_HANDLE;
    allocation = VK_NULL_HANDLE;
}

static void cleanup_swapchain_targets(VulkanEngine* engine) {
    if (engine->device == VK_NULL_HANDLE) {
        return;
    }

    for (uint32_t i = 0; i < engine->imageCount; ++i) {
        destroy_device_handle(engine->device, engine->swapchainFramebuffers[i], vkDestroyFramebuffer);
        destroy_device_handle(engine->device, engine->swapchainImageViews[i], vkDestroyImageView);
    }
}

static void cleanup_swapchain_dependent_resources(VulkanEngine* engine) {
    destroy_device_handle(engine->device, engine->graphicsPipeline, vkDestroyPipeline);
    destroy_device_handle(engine->device, engine->pipelineLayout, vkDestroyPipelineLayout);
    destroy_device_handle(engine->device, engine->depthImageView, vkDestroyImageView);
    destroy_image_allocation(engine->allocator, engine->depthImage, engine->depthImageAllocation);
    cleanup_swapchain_targets(engine);
    destroy_device_handle(engine->device, engine->renderPass, vkDestroyRenderPass);
    destroy_device_handle(engine->device, engine->swapchain, vkDestroySwapchainKHR);
    engine->imageCount = 0;
}

static void cleanup_sync_objects(VulkanEngine* engine) {
    destroy_device_handle(engine->device, engine->renderFinishedSemaphore, vkDestroySemaphore);
    destroy_device_handle(engine->device, engine->imageAvailableSemaphore, vkDestroySemaphore);
    destroy_device_handle(engine->device, engine->inFlightFence, vkDestroyFence);
}

static void cleanup_descriptor_resources(VulkanEngine* engine) {
    destroy_device_handle(engine->device, engine->descriptorPool, vkDestroyDescriptorPool);
    destroy_device_handle(engine->device, engine->descriptorSetLayout, vkDestroyDescriptorSetLayout);
}

static void cleanup_buffer_resources(VulkanEngine* engine) {
    unmap_allocation(engine->allocator, engine->uniformBufferAllocation, engine->uniformBufferMapped);
    destroy_buffer_allocation(engine->allocator, engine->uniformBuffer, engine->uniformBufferAllocation);
    destroy_buffer_allocation(engine->allocator, engine->vertexBuffer, engine->vertexBufferAllocation);
    destroy_buffer_allocation(engine->allocator, engine->indexBuffer, engine->indexBufferAllocation);
}

static void cleanup_render_resources(VulkanEngine* engine) {
    cleanup_swapchain_dependent_resources(engine);
    destroy_device_handle(engine->device, engine->commandPool, vkDestroyCommandPool);
}

static void cleanup_core_resources(VulkanEngine* engine) {
    if (engine->allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(engine->allocator);
        engine->allocator = VK_NULL_HANDLE;
    }

    if (engine->device != VK_NULL_HANDLE) {
        vkDestroyDevice(engine->device, nullptr);
        engine->device = VK_NULL_HANDLE;
    }

    if (engine->instance != VK_NULL_HANDLE && engine->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(engine->instance, engine->surface, nullptr);
        engine->surface = VK_NULL_HANDLE;
    }

    if (engine->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(engine->instance, nullptr);
        engine->instance = VK_NULL_HANDLE;
    }

    if (engine->window != nullptr) {
        glfwDestroyWindow(engine->window);
        engine->window = nullptr;
    }
}

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

// --- INITIALISATION DES SOUS-SYSTÈMES ---

static bool init_core(VulkanEngine* engine) {
    if (glfwInit() != GLFW_TRUE) {
        return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    engine->window = glfwCreateWindow(800, 600, "Vulkan - Icosphere Full GPU", NULL, NULL);
    if (engine->window == nullptr) {
        return false;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.apiVersion = VK_API_VERSION_1_0;

    uint32_t glfwExtCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtCount);
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (vkCreateInstance(&createInfo, NULL, &engine->instance) != VK_SUCCESS)
        return false;
    if (glfwCreateWindowSurface(engine->instance, engine->window, NULL, &engine->surface) != VK_SUCCESS)
        return false;

    uint32_t deviceCount = 0;
    if (vkEnumeratePhysicalDevices(engine->instance, &deviceCount, NULL) != VK_SUCCESS || deviceCount == 0) {
        return false;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    if (vkEnumeratePhysicalDevices(engine->instance, &deviceCount, devices.data()) != VK_SUCCESS) {
        return false;
    }

    QueueFamilySelection queueSelection;
    for (const auto& physicalDevice : devices) {
        if (!has_required_device_extensions(physicalDevice)) {
            continue;
        }

        queueSelection = find_queue_families(physicalDevice, engine->surface);
        if (!queueSelection.isComplete()) {
            continue;
        }

        if (!device_supports_swapchain(physicalDevice, engine->surface)) {
            continue;
        }

        engine->physicalDevice = physicalDevice;
        engine->graphicsQueueFamilyIndex = queueSelection.graphicsFamily;
        engine->presentQueueFamilyIndex = queueSelection.presentFamily;
        break;
    }

    if (engine->physicalDevice == VK_NULL_HANDLE) {
        return false;
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfos[2] = {};
    queueInfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfos[0].queueFamilyIndex = engine->graphicsQueueFamilyIndex;
    queueInfos[0].queueCount = 1;
    queueInfos[0].pQueuePriorities = &queuePriority;

    uint32_t queueInfoCount = 1;
    if (engine->presentQueueFamilyIndex != engine->graphicsQueueFamilyIndex) {
        queueInfos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfos[1].queueFamilyIndex = engine->presentQueueFamilyIndex;
        queueInfos[1].queueCount = 1;
        queueInfos[1].pQueuePriorities = &queuePriority;
        queueInfoCount = 2;
    }

    const char* deviceExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = queueInfoCount;
    deviceInfo.pQueueCreateInfos = queueInfos;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExt;

    if (vkCreateDevice(engine->physicalDevice, &deviceInfo, NULL, &engine->device) != VK_SUCCESS)
        return false;
    vk_set_object_name(engine->device, (uint64_t)engine->device, VK_OBJECT_TYPE_DEVICE, "Logical_Device");

    vkGetDeviceQueue(engine->device, engine->graphicsQueueFamilyIndex, 0, &engine->graphicsQueue);
    vkGetDeviceQueue(engine->device, engine->presentQueueFamilyIndex, 0, &engine->presentQueue);
    return true;
}

static bool init_allocator(VulkanEngine* engine) {
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = engine->physicalDevice;
    allocatorInfo.device = engine->device;
    allocatorInfo.instance = engine->instance;
    return (vmaCreateAllocator(&allocatorInfo, &engine->allocator) == VK_SUCCESS);
}

static bool init_swapchain(VulkanEngine* engine) {
    VkSurfaceCapabilitiesKHR capabilities{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(engine->physicalDevice, engine->surface, &capabilities) != VK_SUCCESS) {
        return false;
    }

    uint32_t formatCount = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(engine->physicalDevice, engine->surface, &formatCount, nullptr) != VK_SUCCESS || formatCount == 0) {
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(engine->physicalDevice, engine->surface, &formatCount, formats.data()) != VK_SUCCESS) {
        return false;
    }

    uint32_t presentModeCount = 0;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(engine->physicalDevice, engine->surface, &presentModeCount, nullptr) != VK_SUCCESS || presentModeCount == 0) {
        return false;
    }
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(engine->physicalDevice, engine->surface, &presentModeCount, presentModes.data()) != VK_SUCCESS) {
        return false;
    }

    const VkSurfaceFormatKHR surfaceFormat = choose_surface_format(formats);
    const VkPresentModeKHR presentMode = choose_present_mode(presentModes);
    engine->swapchainImageFormat = surfaceFormat.format;
    engine->swapchainExtent = choose_swapchain_extent(engine->window, capabilities);

    uint32_t requestedImageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && requestedImageCount > capabilities.maxImageCount) {
        requestedImageCount = capabilities.maxImageCount;
    }
    requestedImageCount = std::min(requestedImageCount, static_cast<uint32_t>(MAX_SWAPCHAIN_IMAGES));
    if (requestedImageCount < capabilities.minImageCount) {
        return false;
    }

    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0) {
        return false;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = engine->surface;
    createInfo.minImageCount = requestedImageCount;
    createInfo.imageFormat = engine->swapchainImageFormat;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = engine->swapchainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    const uint32_t queueFamilyIndices[] = {engine->graphicsQueueFamilyIndex, engine->presentQueueFamilyIndex};
    if (engine->graphicsQueueFamilyIndex != engine->presentQueueFamilyIndex) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = choose_composite_alpha(capabilities.supportedCompositeAlpha);
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(engine->device, &createInfo, NULL, &engine->swapchain) != VK_SUCCESS)
        return false;
    vk_set_object_name(engine->device, (uint64_t)engine->swapchain, VK_OBJECT_TYPE_SWAPCHAIN_KHR, "Main_Swapchain");

    if (vkGetSwapchainImagesKHR(engine->device, engine->swapchain, &engine->imageCount, NULL) != VK_SUCCESS) {
        return false;
    }
    if (engine->imageCount > MAX_SWAPCHAIN_IMAGES) {
        return false;
    }
    if (vkGetSwapchainImagesKHR(engine->device, engine->swapchain, &engine->imageCount, engine->swapchainImages) != VK_SUCCESS) {
        return false;
    }

    for (uint32_t i = 0; i < engine->imageCount; i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = engine->swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = engine->swapchainImageFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(engine->device, &viewInfo, NULL, &engine->swapchainImageViews[i]) != VK_SUCCESS) {
            return false;
        }
    }

    engine->depthFormat = find_depth_format(engine->physicalDevice);
    if (engine->depthFormat == VK_FORMAT_UNDEFINED) {
        return false;
    }

    VkImageCreateInfo depthInfo{};
    depthInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthInfo.imageType = VK_IMAGE_TYPE_2D;
    depthInfo.extent = {engine->swapchainExtent.width, engine->swapchainExtent.height, 1};
    depthInfo.mipLevels = 1;
    depthInfo.arrayLayers = 1;
    depthInfo.format = engine->depthFormat;
    depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo depthAllocInfo{};
    depthAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateImage(engine->allocator, &depthInfo, &depthAllocInfo, &engine->depthImage, &engine->depthImageAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->depthImage, VK_OBJECT_TYPE_IMAGE, "Depth_Buffer_Image");

    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = engine->depthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = engine->depthFormat;
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.layerCount = 1;
    const VkResult depthImageViewResult = vkCreateImageView(engine->device, &depthViewInfo, nullptr, &engine->depthImageView);
    return depthImageViewResult == VK_SUCCESS;
}

static bool init_render_pass(VulkanEngine* engine) {
    VkAttachmentDescription attachments[2] = {};
    attachments[0].format = engine->swapchainImageFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format = engine->depthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 2;
    rpInfo.pAttachments = attachments;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(engine->device, &rpInfo, NULL, &engine->renderPass) != VK_SUCCESS)
        return false;
    vk_set_object_name(engine->device, (uint64_t)engine->renderPass, VK_OBJECT_TYPE_RENDER_PASS, "Main_RenderPass");

    for (uint32_t i = 0; i < engine->imageCount; i++) {
        VkImageView fbAtt[] = {engine->swapchainImageViews[i], engine->depthImageView};
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = engine->renderPass;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments = fbAtt;
        fbInfo.width = engine->swapchainExtent.width;
        fbInfo.height = engine->swapchainExtent.height;
        fbInfo.layers = 1;
        if (vkCreateFramebuffer(engine->device, &fbInfo, NULL, &engine->swapchainFramebuffers[i]) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

static bool init_descriptor_layout(VulkanEngine* engine) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings = &binding;
    return vkCreateDescriptorSetLayout(engine->device, &info, nullptr, &engine->descriptorSetLayout) == VK_SUCCESS;
}

static VkShaderModule load_shader(VkDevice device, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f)
        return VK_NULL_HANDLE;
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint32_t> buffer(size / 4);
    fread(buffer.data(), 1, size, f);
    fclose(f);
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode = buffer.data();
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &mod) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return mod;
}

static bool init_pipeline(VulkanEngine* engine) {
    VkShaderModule vsm = load_shader(engine->device, "shaders/vert.spv");
    VkShaderModule fsm = load_shader(engine->device, "shaders/frag.spv");
    if (vsm == VK_NULL_HANDLE || fsm == VK_NULL_HANDLE) {
        if (vsm != VK_NULL_HANDLE) {
            vkDestroyShaderModule(engine->device, vsm, nullptr);
        }
        if (fsm != VK_NULL_HANDLE) {
            vkDestroyShaderModule(engine->device, fsm, nullptr);
        }
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vsm;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fsm;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2] = {};
    attrs[0].location = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(Vertex, position);
    attrs[1].location = 1;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(Vertex, color);

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{0.f, 0.f, (float)engine->swapchainExtent.width, (float)engine->swapchainExtent.height, 0.f, 1.f};
    VkRect2D sc{{0, 0}, engine->swapchainExtent};
    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.pViewports = &vp;
    vps.scissorCount = 1;
    vps.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &engine->descriptorSetLayout;
    if (vkCreatePipelineLayout(engine->device, &plInfo, nullptr, &engine->pipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(engine->device, vsm, nullptr);
        vkDestroyShaderModule(engine->device, fsm, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeInfo.stageCount = 2;
    pipeInfo.pStages = stages;
    pipeInfo.pVertexInputState = &vi;
    pipeInfo.pInputAssemblyState = &ia;
    pipeInfo.pViewportState = &vps;
    pipeInfo.pRasterizationState = &rs;
    pipeInfo.pMultisampleState = &ms;
    pipeInfo.pColorBlendState = &cb;
    pipeInfo.pDepthStencilState = &ds;
    pipeInfo.layout = engine->pipelineLayout;
    pipeInfo.renderPass = engine->renderPass;

    if (vkCreateGraphicsPipelines(engine->device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &engine->graphicsPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(engine->device, vsm, nullptr);
        vkDestroyShaderModule(engine->device, fsm, nullptr);
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->graphicsPipeline, VK_OBJECT_TYPE_PIPELINE, "Main_Graphics_Pipeline");

    vkDestroyShaderModule(engine->device, vsm, nullptr);
    vkDestroyShaderModule(engine->device, fsm, nullptr);
    return true;
}

static bool init_buffers(VulkanEngine* engine) {
    Icosphere sphere;
    sphere.generate(3);
    engine->indexCount = static_cast<uint32_t>(sphere.indices.size());

    VkCommandPoolCreateInfo cpIn{};
    cpIn.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpIn.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpIn.queueFamilyIndex = engine->graphicsQueueFamilyIndex;
    if (vkCreateCommandPool(engine->device, &cpIn, nullptr, &engine->commandPool) != VK_SUCCESS) {
        return false;
    }

    auto create_gpu_buffer = [&](VkDeviceSize size, VkBufferUsageFlags usage, const void* srcData, VkBuffer& buf, VmaAllocation& alloc,
                                 const char* name) -> bool {
        VkBuffer staging = VK_NULL_HANDLE;
        VmaAllocation stgAlloc = VK_NULL_HANDLE;
        VkBufferCreateInfo stgIn{};
        stgIn.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stgIn.size = size;
        stgIn.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo stgAl{};
        stgAl.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        if (vmaCreateBuffer(engine->allocator, &stgIn, &stgAl, &staging, &stgAlloc, nullptr) != VK_SUCCESS) {
            return false;
        }

        void* map = nullptr;
        if (vmaMapMemory(engine->allocator, stgAlloc, &map) != VK_SUCCESS) {
            vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
            return false;
        }
        memcpy(map, srcData, size);
        vmaUnmapMemory(engine->allocator, stgAlloc);

        VkBufferCreateInfo gpuIn{};
        gpuIn.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        gpuIn.size = size;
        gpuIn.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo gpuAl{};
        gpuAl.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateBuffer(engine->allocator, &gpuIn, &gpuAl, &buf, &alloc, nullptr) != VK_SUCCESS) {
            vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
            return false;
        }
        vk_set_object_name(engine->device, (uint64_t)buf, VK_OBJECT_TYPE_BUFFER, name);

        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = engine->commandPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;

        VkCommandBuffer cb = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(engine->device, &ai, &cb) != VK_SUCCESS) {
            vmaDestroyBuffer(engine->allocator, buf, alloc);
            buf = VK_NULL_HANDLE;
            alloc = VK_NULL_HANDLE;
            vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
            return false;
        }
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS) {
            vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &cb);
            vmaDestroyBuffer(engine->allocator, buf, alloc);
            buf = VK_NULL_HANDLE;
            alloc = VK_NULL_HANDLE;
            vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
            return false;
        }

        vk_begin_label(engine->device, cb, "GPU_Staging_Copy", 0.0f, 1.0f, 0.0f);
        VkBufferCopy cp{};
        cp.size = size;
        vkCmdCopyBuffer(cb, staging, buf, 1, &cp);
        vk_end_label(engine->device, cb);

        if (vkEndCommandBuffer(cb) != VK_SUCCESS) {
            vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &cb);
            vmaDestroyBuffer(engine->allocator, buf, alloc);
            buf = VK_NULL_HANDLE;
            alloc = VK_NULL_HANDLE;
            vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
            return false;
        }

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;

        if (vkQueueSubmit(engine->graphicsQueue, 1, &si, nullptr) != VK_SUCCESS || vkQueueWaitIdle(engine->graphicsQueue) != VK_SUCCESS) {
            vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &cb);
            vmaDestroyBuffer(engine->allocator, buf, alloc);
            buf = VK_NULL_HANDLE;
            alloc = VK_NULL_HANDLE;
            vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
            return false;
        }

        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &cb);
        vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
        return true;
    };

    if (!create_gpu_buffer(sphere.vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, sphere.vertices.data(), engine->vertexBuffer,
                           engine->vertexBufferAllocation, "Icosphere_Vertex_Buffer")) {
        return false;
    }
    if (!create_gpu_buffer(sphere.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, sphere.indices.data(), engine->indexBuffer,
                           engine->indexBufferAllocation, "Icosphere_Index_Buffer")) {
        return false;
    }

    VkBufferCreateInfo uboIn{};
    uboIn.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    uboIn.size = sizeof(glm::mat4);
    uboIn.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VmaAllocationCreateInfo uboAl{};
    uboAl.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    if (vmaCreateBuffer(engine->allocator, &uboIn, &uboAl, &engine->uniformBuffer, &engine->uniformBufferAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->uniformBuffer, VK_OBJECT_TYPE_BUFFER, "Global_MVP_UBO");
    const VkResult uniformBufferMapResult = vmaMapMemory(engine->allocator, engine->uniformBufferAllocation, &engine->uniformBufferMapped);
    return uniformBufferMapResult == VK_SUCCESS;
}

static bool init_descriptor_pool_and_sets(VulkanEngine* engine) {
    VkDescriptorPoolSize sz{};
    sz.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sz.descriptorCount = 1;

    VkDescriptorPoolCreateInfo pIn{};
    pIn.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pIn.maxSets = 1;
    pIn.poolSizeCount = 1;
    pIn.pPoolSizes = &sz;
    if (vkCreateDescriptorPool(engine->device, &pIn, nullptr, &engine->descriptorPool) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = engine->descriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &engine->descriptorSetLayout;
    if (vkAllocateDescriptorSets(engine->device, &ai, &engine->descriptorSet) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorBufferInfo bi{};
    bi.buffer = engine->uniformBuffer;
    bi.offset = 0;
    bi.range = sizeof(glm::mat4);

    VkWriteDescriptorSet wr{};
    wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = engine->descriptorSet;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wr.pBufferInfo = &bi;
    vkUpdateDescriptorSets(engine->device, 1, &wr, 0, nullptr);
    return true;
}

static bool init_commands_and_sync(VulkanEngine* engine) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = engine->commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(engine->device, &ai, &engine->commandBuffer) != VK_SUCCESS) {
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->commandBuffer, VK_OBJECT_TYPE_COMMAND_BUFFER, "Main_CommandBuffer");

    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateSemaphore(engine->device, &si, nullptr, &engine->imageAvailableSemaphore) != VK_SUCCESS) {
        return false;
    }
    if (vkCreateSemaphore(engine->device, &si, nullptr, &engine->renderFinishedSemaphore) != VK_SUCCESS) {
        return false;
    }
    if (vkCreateFence(engine->device, &fi, nullptr, &engine->inFlightFence) != VK_SUCCESS) {
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->inFlightFence, VK_OBJECT_TYPE_FENCE, "Main_Render_Fence");
    return true;
}

static void update_animation_clock(VulkanEngine* engine) {
    const auto now = std::chrono::steady_clock::now();

    if (engine->lastFrameTimestamp.time_since_epoch().count() == 0) {
        engine->lastFrameTimestamp = now;
        return;
    }

    float deltaSeconds = std::chrono::duration<float>(now - engine->lastFrameTimestamp).count();
    engine->lastFrameTimestamp = now;
    deltaSeconds = std::clamp(deltaSeconds, 0.0f, 0.25f);

    if (!engine->animationPaused) {
        engine->animationTimeSeconds += deltaSeconds * engine->animationSpeed;
    }
}

static bool recreate_swapchain_dependent_resources(VulkanEngine* engine) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(engine->window, &width, &height);
    while (width == 0 || height == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(engine->window, &width, &height);
    }

    if (vkDeviceWaitIdle(engine->device) != VK_SUCCESS) {
        return false;
    }

    cleanup_swapchain_dependent_resources(engine);

    return init_swapchain(engine) && init_render_pass(engine) && init_pipeline(engine);
}

// --- FONCTIONS PUBLIQUES ---

bool init_vulkan_engine(VulkanEngine* engine) {
    if (!init_core(engine) || !init_allocator(engine) || !init_swapchain(engine) || !init_render_pass(engine) || !init_descriptor_layout(engine) ||
        !init_pipeline(engine) || !init_buffers(engine) || !init_descriptor_pool_and_sets(engine) || !init_commands_and_sync(engine)) {
        cleanup_vulkan_engine(engine);
        return false;
    }
    engine->animationTimeSeconds = 0.0f;
    engine->animationSpeed = 1.0f;
    engine->animationPaused = false;
    engine->pauseKeyWasDown = false;
    engine->resetKeyWasDown = false;
    engine->speedUpKeyWasDown = false;
    engine->speedDownKeyWasDown = false;
    engine->fullscreenKeyWasDown = false;
    engine->escapeKeyWasDown = false;
    engine->isFullscreen = false;
    glfwGetWindowPos(engine->window, &engine->windowedPosX, &engine->windowedPosY);
    glfwGetWindowSize(engine->window, &engine->windowedWidth, &engine->windowedHeight);
    engine->lastFrameTimestamp = std::chrono::steady_clock::now();
    LOG_INFO("engine", "Vulkan initialise avec succes !");
    return true;
}

bool draw_frame(VulkanEngine* engine) {
    if (vkWaitForFences(engine->device, 1, &engine->inFlightFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        return false;
    }
    if (vkResetFences(engine->device, 1, &engine->inFlightFence) != VK_SUCCESS) {
        return false;
    }

    uint32_t idx;
    const VkResult acquireResult = vkAcquireNextImageKHR(engine->device, engine->swapchain, UINT64_MAX, engine->imageAvailableSemaphore, nullptr, &idx);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        return recreate_swapchain_dependent_resources(engine);
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        return false;
    }
    engine->lastRenderedImageIndex = idx;

    update_animation_clock(engine);

    glm::mat4 model = glm::rotate(glm::mat4(1.0f), engine->animationTimeSeconds * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.f),
                                      static_cast<float>(engine->swapchainExtent.width) / static_cast<float>(engine->swapchainExtent.height), 0.1f, 10.f);
    proj[1][1] *= -1;
    glm::mat4 mvp = proj * view * model;
    memcpy(engine->uniformBufferMapped, &mvp, sizeof(mvp));

    if (vkResetCommandBuffer(engine->commandBuffer, 0) != VK_SUCCESS) {
        return false;
    }
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(engine->commandBuffer, &bi) != VK_SUCCESS) {
        return false;
    }

    vk_begin_label(engine->device, engine->commandBuffer, "Render_Icosphere_Pass", 1.0f, 0.5f, 0.0f);

    VkClearValue cl[2] = {};
    cl[0].color = {{0.05f, 0.05f, 0.2f, 1.0f}};
    cl[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = engine->renderPass;
    rp.framebuffer = engine->swapchainFramebuffers[idx];
    rp.renderArea.extent = engine->swapchainExtent;
    rp.clearValueCount = 2;
    rp.pClearValues = cl;

    vkCmdBeginRenderPass(engine->commandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(engine->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, engine->graphicsPipeline);

    VkDeviceSize off[] = {0};
    vkCmdBindVertexBuffers(engine->commandBuffer, 0, 1, &engine->vertexBuffer, off);
    vkCmdBindIndexBuffer(engine->commandBuffer, engine->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(engine->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, engine->pipelineLayout, 0, 1, &engine->descriptorSet, 0, nullptr);
    vkCmdDrawIndexed(engine->commandBuffer, engine->indexCount, 1, 0, 0, 0);

    vkCmdEndRenderPass(engine->commandBuffer);
    vk_end_label(engine->device, engine->commandBuffer);
    if (vkEndCommandBuffer(engine->commandBuffer) != VK_SUCCESS) {
        return false;
    }

    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &engine->imageAvailableSemaphore;
    si.pWaitDstStageMask = &wait;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &engine->commandBuffer;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &engine->renderFinishedSemaphore;

    if (vkQueueSubmit(engine->graphicsQueue, 1, &si, engine->inFlightFence) != VK_SUCCESS) {
        return false;
    }

    VkPresentInfoKHR pri{};
    pri.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pri.waitSemaphoreCount = 1;
    pri.pWaitSemaphores = &engine->renderFinishedSemaphore;
    pri.swapchainCount = 1;
    pri.pSwapchains = &engine->swapchain;
    pri.pImageIndices = &idx;
    const VkResult presentResult = vkQueuePresentKHR(engine->presentQueue, &pri);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        return recreate_swapchain_dependent_resources(engine);
    }
    return presentResult == VK_SUCCESS;
}

void cleanup_vulkan_engine(VulkanEngine* engine) {
    if (engine->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(engine->device);
    }

    cleanup_sync_objects(engine);
    cleanup_descriptor_resources(engine);
    cleanup_buffer_resources(engine);
    cleanup_render_resources(engine);
    cleanup_core_resources(engine);
    glfwTerminate();
}
