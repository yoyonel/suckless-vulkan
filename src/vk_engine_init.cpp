#include "vk_engine_init.h"
#include "app_log.h"
#include "camera.h"
#include "icosphere.h"
#include "material_loader.h"
#include "rhi/null_rhi.h"
#include "rhi/vulkan_rhi.h"
#include "tracy_client.h"
#include "tracy_vulkan.h"
#include "vk_engine_envmap.h"
#include "vk_engine_runtime.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kGridSize = 10;
constexpr float kGridSpacing = 2.5f;
constexpr float kGridOffset = (static_cast<float>(kGridSize) - 1.0f) * 0.5f * kGridSpacing;
constexpr size_t kMaterialInstanceCount = static_cast<size_t>(kGridSize) * static_cast<size_t>(kGridSize);
constexpr const char* kMaterialJsonPath = "assets/materials/pbr_materials.json";
constexpr int kLegacyWindowWidth = 1024;
constexpr int kLegacyWindowHeight = 768;

MaterialGpu make_default_material() {
    MaterialGpu material{};
    material.albedo_metallic[0] = 0.0f;
    material.albedo_metallic[1] = 0.0f;
    material.albedo_metallic[2] = 0.0f;
    material.albedo_metallic[3] = 0.0f;
    material.roughness_ao_pad[0] = 0.5f;
    material.roughness_ao_pad[1] = 1.0f;
    material.roughness_ao_pad[2] = 0.0f;
    material.roughness_ao_pad[3] = 0.0f;
    return material;
}

bool load_legacy_materials_for_grid(std::vector<MaterialGpu>& materials, size_t instanceCount) {
    if (!MaterialLoader::load_materials(kMaterialJsonPath, materials)) {
        LOG_ERROR("material", "Failed to load material presets from %s", kMaterialJsonPath);
        return false;
    }

    if (materials.size() != instanceCount) {
        if (materials.size() < instanceCount) {
            LOG_WARNING("material", "Material preset count (%zu) is lower than instance count (%zu); filling missing entries with legacy defaults.",
                        materials.size(), instanceCount);
        }
        materials.resize(instanceCount, make_default_material());
    }

    return true;
}

struct QueueFamilySelection {
    uint32_t graphicsFamily = UINT32_MAX;
    uint32_t presentFamily = UINT32_MAX;

    bool isComplete() const {
        return graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX;
    }
};

bool has_required_device_extensions(VkPhysicalDevice physicalDevice) {
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

QueueFamilySelection find_queue_families(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
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

bool device_supports_swapchain(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
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

VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& format : availableFormats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
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
        } else if (find_mode(VK_PRESENT_MODE_FIFO_KHR)) {
            selected = VK_PRESENT_MODE_FIFO_KHR;
        }
    } else {
        // Preferred Un-capped/Tearing mode
        if (find_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)) {
            selected = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
    }

    const char* selectedName = "UNKNOWN";
    if (selected == VK_PRESENT_MODE_IMMEDIATE_KHR) {
        selectedName = "IMMEDIATE";
    } else if (selected == VK_PRESENT_MODE_MAILBOX_KHR) {
        selectedName = "MAILBOX";
    } else if (selected == VK_PRESENT_MODE_FIFO_KHR) {
        selectedName = "FIFO";
    }
    LOG_INFO("engine", "Selected present mode: %s", selectedName);

    return selected;
}

VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supportedCompositeAlpha) {
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

VkExtent2D choose_swapchain_extent(GLFWwindow* window, const VkSurfaceCapabilitiesKHR& capabilities) {
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

VkFormat find_depth_format(VkPhysicalDevice physicalDevice) {
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

template <typename Handle, typename DestroyFn> void destroy_device_handle(VkDevice device, Handle& handle, DestroyFn destroyFn) {
    if (device == VK_NULL_HANDLE || handle == VK_NULL_HANDLE) {
        return;
    }

    destroyFn(device, handle, nullptr);
    handle = VK_NULL_HANDLE;
}

void unmap_allocation(VmaAllocator allocator, VmaAllocation allocation, void*& mappedMemory) {
    if (allocator == VK_NULL_HANDLE || allocation == VK_NULL_HANDLE || mappedMemory == nullptr) {
        return;
    }

    vmaUnmapMemory(allocator, allocation);
    mappedMemory = nullptr;
}

void destroy_buffer_allocation(VmaAllocator allocator, VkBuffer& buffer, VmaAllocation& allocation) {
    if (allocator == VK_NULL_HANDLE || buffer == VK_NULL_HANDLE || allocation == VK_NULL_HANDLE) {
        return;
    }

    vmaDestroyBuffer(allocator, buffer, allocation);
    buffer = VK_NULL_HANDLE;
    allocation = VK_NULL_HANDLE;
}

void destroy_image_allocation(VmaAllocator allocator, VkImage& image, VmaAllocation& allocation) {
    if (allocator == VK_NULL_HANDLE || image == VK_NULL_HANDLE || allocation == VK_NULL_HANDLE) {
        return;
    }

    vmaDestroyImage(allocator, image, allocation);
    image = VK_NULL_HANDLE;
    allocation = VK_NULL_HANDLE;
}

void cleanup_swapchain_targets(VulkanEngine* engine) {
    if (engine->device == VK_NULL_HANDLE) {
        return;
    }

    for (uint32_t i = 0; i < engine->imageCount; ++i) {
        destroy_device_handle(engine->device, engine->swapchainFramebuffers[i], vkDestroyFramebuffer);
        destroy_device_handle(engine->device, engine->swapchainImageViews[i], vkDestroyImageView);
    }
}

void cleanup_swapchain_dependent_resources(VulkanEngine* engine) {
    destroy_device_handle(engine->device, engine->graphicsPipeline, vkDestroyPipeline);
    destroy_device_handle(engine->device, engine->skyboxPipeline, vkDestroyPipeline);
    destroy_device_handle(engine->device, engine->billboardPipeline, vkDestroyPipeline);
    destroy_device_handle(engine->device, engine->wireframePipeline, vkDestroyPipeline);
    destroy_device_handle(engine->device, engine->debugLinePipeline, vkDestroyPipeline);
    destroy_device_handle(engine->device, engine->debugTrianglePipeline, vkDestroyPipeline);
    destroy_device_handle(engine->device, engine->pipelineLayout, vkDestroyPipelineLayout);
    destroy_device_handle(engine->device, engine->debugPipelineLayout, vkDestroyPipelineLayout);
    destroy_device_handle(engine->device, engine->depthImageView, vkDestroyImageView);
    destroy_image_allocation(engine->allocator, engine->depthImage, engine->depthImageAllocation);
    cleanup_swapchain_targets(engine);
    destroy_device_handle(engine->device, engine->renderPass, vkDestroyRenderPass);
    destroy_device_handle(engine->device, engine->swapchain, vkDestroySwapchainKHR);
    engine->imageCount = 0;
}

void cleanup_sync_objects(VulkanEngine* engine) {
    destroy_device_handle(engine->device, engine->renderFinishedSemaphore, vkDestroySemaphore);
    destroy_device_handle(engine->device, engine->imageAvailableSemaphore, vkDestroySemaphore);
    destroy_device_handle(engine->device, engine->inFlightFence, vkDestroyFence);
}

void cleanup_descriptor_resources(VulkanEngine* engine) {
    destroy_device_handle(engine->device, engine->descriptorPool, vkDestroyDescriptorPool);
    destroy_device_handle(engine->device, engine->descriptorSetLayout, vkDestroyDescriptorSetLayout);
}

void cleanup_buffer_resources(VulkanEngine* engine) {
    unmap_allocation(engine->allocator, engine->uniformBufferAllocation, engine->uniformBufferMapped);
    destroy_buffer_allocation(engine->allocator, engine->uniformBuffer, engine->uniformBufferAllocation);
    unmap_allocation(engine->allocator, engine->billboardAllocation, engine->billboardMapped);
    destroy_buffer_allocation(engine->allocator, engine->billboardBuffer, engine->billboardAllocation);
    destroy_buffer_allocation(engine->allocator, engine->materialBuffer, engine->materialBufferAllocation);
    destroy_buffer_allocation(engine->allocator, engine->instanceBuffer, engine->instanceBufferAllocation);
    destroy_buffer_allocation(engine->allocator, engine->vertexBuffer, engine->vertexBufferAllocation);
    destroy_buffer_allocation(engine->allocator, engine->indexBuffer, engine->indexBufferAllocation);
    vk_cleanup_environment_resources(engine);
}

void cleanup_render_resources(VulkanEngine* engine) {
    cleanup_swapchain_dependent_resources(engine);
    destroy_device_handle(engine->device, engine->commandPool, vkDestroyCommandPool);
}

void cleanup_core_resources(VulkanEngine* engine) {
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

bool init_core(VulkanEngine* engine) {
    if (glfwInit() != GLFW_TRUE) {
        return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    // Keep startup viewport ISO with legacy OpenGL app (1024x768).
    engine->window = glfwCreateWindow(kLegacyWindowWidth, kLegacyWindowHeight, "Vulkan - Icosphere Full GPU", NULL, NULL);
    if (engine->window == nullptr) {
        return false;
    }

    glfwSetWindowUserPointer(engine->window, engine);
    glfwSetCursorPosCallback(engine->window, vk_mouse_callback);
    glfwSetScrollCallback(engine->window, vk_scroll_callback);

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
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE;
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = queueInfoCount;
    deviceInfo.pQueueCreateInfos = queueInfos;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExt;
    deviceInfo.pEnabledFeatures = &deviceFeatures;

    if (vkCreateDevice(engine->physicalDevice, &deviceInfo, NULL, &engine->device) != VK_SUCCESS)
        return false;
    vk_set_object_name(engine->device, (uint64_t)engine->device, VK_OBJECT_TYPE_DEVICE, "Logical_Device");

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(engine->physicalDevice, &props);
    const uint32_t v = props.driverVersion;
    LOG_INFO("suckless-vulkan.window", "Context Version: %u.%u", VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion));
    LOG_INFO("suckless-vulkan.window", "Renderer: %s", props.deviceName);
    LOG_INFO("suckless-vulkan.window", "Version: %u.%u.%u (Driver)", VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v));

    vkGetDeviceQueue(engine->device, engine->graphicsQueueFamilyIndex, 0, &engine->graphicsQueue);
    vkGetDeviceQueue(engine->device, engine->presentQueueFamilyIndex, 0, &engine->presentQueue);
    vk_set_object_name(engine->device, (uint64_t)engine->graphicsQueue, VK_OBJECT_TYPE_QUEUE, "Graphics_Queue");
    vk_set_object_name(engine->device, (uint64_t)engine->presentQueue, VK_OBJECT_TYPE_QUEUE, "Present_Queue");
    return true;
}

bool init_allocator(VulkanEngine* engine) {
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = engine->physicalDevice;
    allocatorInfo.device = engine->device;
    allocatorInfo.instance = engine->instance;
    return (vmaCreateAllocator(&allocatorInfo, &engine->allocator) == VK_SUCCESS);
}

bool init_swapchain(VulkanEngine* engine) {
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
    const VkPresentModeKHR presentMode = choose_present_mode(presentModes, engine->core.vsync);
    engine->swapchainImageFormat = surfaceFormat.format;
    engine->swapchainExtent = choose_swapchain_extent(engine->window, capabilities);

    LOG_INFO("engine", "Swapchain Extent: %ux%u", engine->swapchainExtent.width, engine->swapchainExtent.height);

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    LOG_INFO("engine", "Swapchain: minImageCount=%u, maxImageCount=%u, using imageCount=%u", capabilities.minImageCount, capabilities.maxImageCount,
             imageCount);

    imageCount = std::min(imageCount, static_cast<uint32_t>(MAX_SWAPCHAIN_IMAGES));
    if (imageCount < capabilities.minImageCount) {
        return false;
    }

    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0) {
        return false;
    }

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = engine->surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = engine->swapchainImageFormat;
    swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainInfo.imageExtent = engine->swapchainExtent;
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

    if (vkCreateSwapchainKHR(engine->device, &swapchainInfo, NULL, &engine->swapchain) != VK_SUCCESS)
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
        const std::string swapchainViewName = "Swapchain_ImageView_" + std::to_string(i);
        vk_set_object_name(engine->device, (uint64_t)engine->swapchainImageViews[i], VK_OBJECT_TYPE_IMAGE_VIEW, swapchainViewName.c_str());
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
    if (depthImageViewResult != VK_SUCCESS) {
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->depthImageView, VK_OBJECT_TYPE_IMAGE_VIEW, "Depth_Buffer_ImageView");
    return true;
}

bool init_render_pass(VulkanEngine* engine) {
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
        const std::string framebufferName = "Swapchain_Framebuffer_" + std::to_string(i);
        vk_set_object_name(engine->device, (uint64_t)engine->swapchainFramebuffers[i], VK_OBJECT_TYPE_FRAMEBUFFER, framebufferName.c_str());
    }
    return true;
}

bool init_descriptor_layout(VulkanEngine* engine) {
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        // Binding 0: Uniform Buffer (MVP + Config)
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        // Binding 1: HDR Environment Map
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        // Binding 2: Irradiance Map
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        // Binding 3: Prefiltered Specular Map
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        // Binding 4: BRDF LUT
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        // Binding 5: Array of Materials (SSBO)
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(engine->device, &info, nullptr, &engine->descriptorSetLayout) != VK_SUCCESS) {
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->descriptorSetLayout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "Global_DescriptorSetLayout");
    return true;
}

VkShaderModule load_shader(VkDevice device, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f)
        return VK_NULL_HANDLE;
    fseek(f, 0, SEEK_END);
    size_t size = static_cast<size_t>(ftell(f));
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

struct PipelineCommonState {
    VkPipelineInputAssemblyStateCreateInfo ia;
    VkViewport vp;
    VkRect2D sc;
    VkPipelineViewportStateCreateInfo vps;
    VkPipelineRasterizationStateCreateInfo rs;
    VkPipelineMultisampleStateCreateInfo ms;
    VkPipelineColorBlendAttachmentState cba;
    VkPipelineColorBlendStateCreateInfo cb;
    VkPipelineDepthStencilStateCreateInfo ds;
};

void init_common_pipeline_state(VulkanEngine* engine, PipelineCommonState& state) {
    state.ia = {};
    state.ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    state.ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    state.vp = {0.f, 0.f, (float)engine->swapchainExtent.width, (float)engine->swapchainExtent.height, 0.f, 1.f};
    state.sc = {{0, 0}, engine->swapchainExtent};

    state.vps = {};
    state.vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    state.vps.viewportCount = 1;
    state.vps.pViewports = &state.vp;
    state.vps.scissorCount = 1;
    state.vps.pScissors = &state.sc;

    state.rs = {};
    state.rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    state.rs.polygonMode = VK_POLYGON_MODE_FILL;
    state.rs.lineWidth = 1.0f;
    state.rs.cullMode = VK_CULL_MODE_BACK_BIT;
    state.rs.frontFace = VK_FRONT_FACE_CLOCKWISE;

    state.ms = {};
    state.ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    state.ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    state.cba = {};
    state.cba.colorWriteMask = 0xf;

    state.cb = {};
    state.cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    state.cb.attachmentCount = 1;
    state.cb.pAttachments = &state.cba;

    state.ds = {};
    state.ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    state.ds.depthTestEnable = VK_TRUE;
    state.ds.depthWriteEnable = VK_TRUE;
    state.ds.depthCompareOp = VK_COMPARE_OP_LESS;
}

bool create_main_graphics_pipeline(VulkanEngine* engine, const PipelineCommonState& common, VkShaderModule vsm, VkShaderModule fsm) {
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vsm;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fsm;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bindings[2] = {};
    bindings[0].binding = 0;
    bindings[0].stride = sizeof(Vertex);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding = 1;
    bindings[1].stride = sizeof(glm::vec3);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attrs[3] = {};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(Vertex, position);
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(Vertex, color);
    attrs[2].location = 2;
    attrs[2].binding = 1;
    attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[2].offset = 0;

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 2;
    vi.pVertexBindingDescriptions = bindings;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;

    VkGraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeInfo.stageCount = 2;
    pipeInfo.pStages = stages;
    pipeInfo.pVertexInputState = &vi;
    pipeInfo.pInputAssemblyState = &common.ia;
    pipeInfo.pViewportState = &common.vps;
    pipeInfo.pRasterizationState = &common.rs;
    pipeInfo.pMultisampleState = &common.ms;
    pipeInfo.pColorBlendState = &common.cb;
    pipeInfo.pDepthStencilState = &common.ds;
    pipeInfo.layout = engine->pipelineLayout;
    pipeInfo.renderPass = engine->renderPass;

    if (vkCreateGraphicsPipelines(engine->device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &engine->graphicsPipeline) != VK_SUCCESS) {
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->graphicsPipeline, VK_OBJECT_TYPE_PIPELINE, "Main_Graphics_Pipeline");
    return true;
}

bool create_skybox_pipeline(VulkanEngine* engine, const PipelineCommonState& common, VkShaderModule vsm, VkShaderModule fsm) {
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vsm;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fsm;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineRasterizationStateCreateInfo rs = common.rs;
    rs.cullMode = VK_CULL_MODE_NONE;

    VkPipelineDepthStencilStateCreateInfo ds = common.ds;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkGraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeInfo.stageCount = 2;
    pipeInfo.pStages = stages;
    pipeInfo.pVertexInputState = &vi;
    pipeInfo.pInputAssemblyState = &common.ia;
    pipeInfo.pViewportState = &common.vps;
    pipeInfo.pRasterizationState = &rs;
    pipeInfo.pMultisampleState = &common.ms;
    pipeInfo.pColorBlendState = &common.cb;
    pipeInfo.pDepthStencilState = &ds;
    pipeInfo.layout = engine->pipelineLayout;
    pipeInfo.renderPass = engine->renderPass;

    if (vkCreateGraphicsPipelines(engine->device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &engine->skyboxPipeline) != VK_SUCCESS) {
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->skyboxPipeline, VK_OBJECT_TYPE_PIPELINE, "Skybox_Graphics_Pipeline");
    return true;
}

bool create_billboard_pipeline(VulkanEngine* engine, const PipelineCommonState& common, VkPipelineVertexInputStateCreateInfo* bVi) {
    VkShaderModule bvm = load_shader(engine->device, "shaders/billboard_vert.spv");
    VkShaderModule bfm = load_shader(engine->device, "shaders/billboard_frag.spv");
    if (bvm == VK_NULL_HANDLE || bfm == VK_NULL_HANDLE) {
        LOG_ERROR("render", "Failed to load billboard shaders");
        if (bvm)
            vkDestroyShaderModule(engine->device, bvm, nullptr);
        if (bfm)
            vkDestroyShaderModule(engine->device, bfm, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo bStages[2] = {};
    bStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    bStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    bStages[0].module = bvm;
    bStages[0].pName = "main";
    bStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    bStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    bStages[1].module = bfm;
    bStages[1].pName = "main";

    VkPipelineRasterizationStateCreateInfo bRs = common.rs;
    bRs.cullMode = VK_CULL_MODE_NONE;

    VkPipelineColorBlendAttachmentState bCba = common.cba;
    bCba.blendEnable = VK_TRUE;
    bCba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    bCba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    bCba.colorBlendOp = VK_BLEND_OP_ADD;
    bCba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    bCba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    bCba.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo bCb = common.cb;
    bCb.attachmentCount = 1;
    bCb.pAttachments = &bCba;

    VkGraphicsPipelineCreateInfo bPipeInfo{};
    bPipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    bPipeInfo.stageCount = 2;
    bPipeInfo.pStages = bStages;
    bPipeInfo.pVertexInputState = bVi;
    bPipeInfo.pInputAssemblyState = &common.ia;
    bPipeInfo.pViewportState = &common.vps;
    bPipeInfo.pRasterizationState = &bRs;
    bPipeInfo.pMultisampleState = &common.ms;
    bPipeInfo.pColorBlendState = &bCb;
    bPipeInfo.pDepthStencilState = &common.ds;
    bPipeInfo.layout = engine->pipelineLayout;
    bPipeInfo.renderPass = engine->renderPass;

    bool success = true;
    if (vkCreateGraphicsPipelines(engine->device, VK_NULL_HANDLE, 1, &bPipeInfo, nullptr, &engine->billboardPipeline) != VK_SUCCESS) {
        LOG_ERROR("render", "Failed to create billboard pipeline");
        success = false;
    } else {
        vk_set_object_name(engine->device, (uint64_t)engine->billboardPipeline, VK_OBJECT_TYPE_PIPELINE, "Billboard_Graphics_Pipeline");
    }

    vkDestroyShaderModule(engine->device, bvm, nullptr);
    vkDestroyShaderModule(engine->device, bfm, nullptr);
    return success;
}

bool create_wireframe_pipeline(VulkanEngine* engine, const PipelineCommonState& common) {
    VkShaderModule vsm = load_shader(engine->device, "shaders/vert.spv");
    VkShaderModule fsm = load_shader(engine->device, "shaders/frag.spv");
    if (vsm == VK_NULL_HANDLE || fsm == VK_NULL_HANDLE) {
        if (vsm)
            vkDestroyShaderModule(engine->device, vsm, nullptr);
        if (fsm)
            vkDestroyShaderModule(engine->device, fsm, nullptr);
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

    VkVertexInputBindingDescription bindings[2] = {};
    bindings[0].binding = 0;
    bindings[0].stride = sizeof(Vertex);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding = 1;
    bindings[1].stride = sizeof(glm::vec3);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    VkVertexInputAttributeDescription attrs[3] = {};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(Vertex, position);
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(Vertex, color);
    attrs[2].location = 2;
    attrs[2].binding = 1;
    attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[2].offset = 0;

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 2;
    vi.pVertexBindingDescriptions = bindings;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineRasterizationStateCreateInfo rs = common.rs;
    rs.polygonMode = VK_POLYGON_MODE_LINE;

    VkGraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeInfo.stageCount = 2;
    pipeInfo.pStages = stages;
    pipeInfo.pVertexInputState = &vi;
    pipeInfo.pInputAssemblyState = &common.ia;
    pipeInfo.pViewportState = &common.vps;
    pipeInfo.pRasterizationState = &rs;
    pipeInfo.pMultisampleState = &common.ms;
    pipeInfo.pColorBlendState = &common.cb;
    pipeInfo.pDepthStencilState = &common.ds;
    pipeInfo.layout = engine->pipelineLayout;
    pipeInfo.renderPass = engine->renderPass;

    bool success = true;
    if (vkCreateGraphicsPipelines(engine->device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &engine->wireframePipeline) != VK_SUCCESS) {
        LOG_ERROR("render", "Failed to create wireframe pipeline");
        success = false;
    } else {
        vk_set_object_name(engine->device, (uint64_t)engine->wireframePipeline, VK_OBJECT_TYPE_PIPELINE, "Wireframe_Graphics_Pipeline");
    }

    vkDestroyShaderModule(engine->device, vsm, nullptr);
    vkDestroyShaderModule(engine->device, fsm, nullptr);
    return success;
}

bool create_debug_pipelines(VulkanEngine* engine, const PipelineCommonState& common, VkPipelineVertexInputStateCreateInfo* bVi) {
    VkShaderModule dvm = load_shader(engine->device, "shaders/debug_vert.spv");
    VkShaderModule dfm = load_shader(engine->device, "shaders/debug_frag.spv");
    if (dvm == VK_NULL_HANDLE || dfm == VK_NULL_HANDLE) {
        if (dvm)
            vkDestroyShaderModule(engine->device, dvm, nullptr);
        if (dfm)
            vkDestroyShaderModule(engine->device, dfm, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo dStages[2] = {};
    dStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    dStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    dStages[0].module = dvm;
    dStages[0].pName = "main";
    dStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    dStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    dStages[1].module = dfm;
    dStages[1].pName = "main";

    VkPushConstantRange dPushRange{};
    dPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    dPushRange.offset = 0;
    dPushRange.size = sizeof(DebugPushConstant);

    VkPipelineLayoutCreateInfo dplInfo{};
    dplInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    dplInfo.setLayoutCount = 1;
    dplInfo.pSetLayouts = &engine->descriptorSetLayout;
    dplInfo.pushConstantRangeCount = 1;
    dplInfo.pPushConstantRanges = &dPushRange;
    if (vkCreatePipelineLayout(engine->device, &dplInfo, nullptr, &engine->debugPipelineLayout) != VK_SUCCESS) {
        LOG_ERROR("render", "Failed to create debug pipeline layout");
    }

    VkPipelineInputAssemblyStateCreateInfo dIaLines = common.ia;
    dIaLines.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    VkPipelineDepthStencilStateCreateInfo dsDebug = common.ds;
    dsDebug.depthWriteEnable = VK_FALSE;
    VkPipelineRasterizationStateCreateInfo dRsLines = common.rs;
    dRsLines.polygonMode = VK_POLYGON_MODE_LINE;

    VkGraphicsPipelineCreateInfo dLineInfo{};
    dLineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    dLineInfo.stageCount = 2;
    dLineInfo.pStages = dStages;
    dLineInfo.pVertexInputState = bVi;
    dLineInfo.pInputAssemblyState = &dIaLines;
    dLineInfo.pViewportState = &common.vps;
    dLineInfo.pRasterizationState = &dRsLines;
    dLineInfo.pMultisampleState = &common.ms;
    dLineInfo.pColorBlendState = &common.cb;
    dLineInfo.pDepthStencilState = &dsDebug;
    dLineInfo.layout = engine->debugPipelineLayout;
    dLineInfo.renderPass = engine->renderPass;

    if (vkCreateGraphicsPipelines(engine->device, VK_NULL_HANDLE, 1, &dLineInfo, nullptr, &engine->debugLinePipeline) != VK_SUCCESS) {
        LOG_ERROR("render", "Failed to create debug line pipeline");
    } else {
        vk_set_object_name(engine->device, (uint64_t)engine->debugLinePipeline, VK_OBJECT_TYPE_PIPELINE, "Debug_Line_Pipeline");
    }

    VkPipelineInputAssemblyStateCreateInfo dIaTris = common.ia;
    dIaTris.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineRasterizationStateCreateInfo dRsTris = common.rs;
    dRsTris.polygonMode = VK_POLYGON_MODE_FILL;
    VkPipelineColorBlendAttachmentState dCbaBlend = common.cba;
    dCbaBlend.blendEnable = VK_TRUE;
    dCbaBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    dCbaBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    dCbaBlend.colorBlendOp = VK_BLEND_OP_ADD;
    dCbaBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    dCbaBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    dCbaBlend.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo dCbBlend = common.cb;
    dCbBlend.attachmentCount = 1;
    dCbBlend.pAttachments = &dCbaBlend;

    VkGraphicsPipelineCreateInfo dTriInfo = dLineInfo;
    dTriInfo.pInputAssemblyState = &dIaTris;
    dTriInfo.pRasterizationState = &dRsTris;
    dTriInfo.pColorBlendState = &dCbBlend;

    if (vkCreateGraphicsPipelines(engine->device, VK_NULL_HANDLE, 1, &dTriInfo, nullptr, &engine->debugTrianglePipeline) != VK_SUCCESS) {
        LOG_ERROR("render", "Failed to create debug triangle pipeline");
    }

    vkDestroyShaderModule(engine->device, dvm, nullptr);
    vkDestroyShaderModule(engine->device, dfm, nullptr);
    return true;
}

bool create_gpu_buffer(VulkanEngine* engine, VkDeviceSize size, VkBufferUsageFlags usage, const void* srcData, VkBuffer& buf, VmaAllocation& alloc,
                       const char* name) {
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
    {
        const std::string stagingBufferName = std::string(name) + "_Staging_Buffer";
        vk_set_object_name(engine->device, (uint64_t)staging, VK_OBJECT_TYPE_BUFFER, stagingBufferName.c_str());
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

    VkCommandBuffer stagingCb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(engine->device, &ai, &stagingCb) != VK_SUCCESS) {
        vmaDestroyBuffer(engine->allocator, buf, alloc);
        buf = VK_NULL_HANDLE;
        alloc = VK_NULL_HANDLE;
        vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
        return false;
    }
    {
        const std::string stagingCbName = std::string(name) + "_Staging_CommandBuffer";
        vk_set_object_name(engine->device, (uint64_t)stagingCb, VK_OBJECT_TYPE_COMMAND_BUFFER, stagingCbName.c_str());
    }
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(stagingCb, &bi) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &stagingCb);
        vmaDestroyBuffer(engine->allocator, buf, alloc);
        buf = VK_NULL_HANDLE;
        alloc = VK_NULL_HANDLE;
        vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
        return false;
    }

    vk_begin_label(engine->device, stagingCb, "GPU_Staging_Copy", 0.0f, 1.0f, 0.0f);
    VkBufferCopy cp{};
    cp.size = size;
    vkCmdCopyBuffer(stagingCb, staging, buf, 1, &cp);
    vk_end_label(engine->device, stagingCb);

    if (vkEndCommandBuffer(stagingCb) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &stagingCb);
        vmaDestroyBuffer(engine->allocator, buf, alloc);
        buf = VK_NULL_HANDLE;
        alloc = VK_NULL_HANDLE;
        vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
        return false;
    }

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &stagingCb;

    if (vkQueueSubmit(engine->graphicsQueue, 1, &si, nullptr) != VK_SUCCESS || vkQueueWaitIdle(engine->graphicsQueue) != VK_SUCCESS) {
        vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &stagingCb);
        vmaDestroyBuffer(engine->allocator, buf, alloc);
        buf = VK_NULL_HANDLE;
        alloc = VK_NULL_HANDLE;
        vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
        return false;
    }

    vkFreeCommandBuffers(engine->device, engine->commandPool, 1, &stagingCb);
    vmaDestroyBuffer(engine->allocator, staging, stgAlloc);
    return true;
}

bool create_icosphere_buffers(VulkanEngine* engine, const Icosphere& sphere) {
    if (!create_gpu_buffer(engine, sphere.vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, sphere.vertices.data(), engine->vertexBuffer,
                           engine->vertexBufferAllocation, "Icosphere_Vertex_Buffer")) {
        return false;
    }
    return create_gpu_buffer(engine, sphere.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, sphere.indices.data(), engine->indexBuffer,
                             engine->indexBufferAllocation, "Icosphere_Index_Buffer");
}

bool create_instance_grid_buffers(VulkanEngine* engine, std::vector<glm::vec3>& instancePositions) {
    const size_t instanceCount = kMaterialInstanceCount;
    instancePositions.resize(instanceCount);
    for (uint32_t row = 0; row < kGridSize; ++row) {
        for (uint32_t col = 0; col < kGridSize; ++col) {
            const size_t instanceIndex = (static_cast<size_t>(row) * static_cast<size_t>(kGridSize)) + static_cast<size_t>(col);
            const float x = (static_cast<float>(col) * kGridSpacing) - kGridOffset;
            const float y = -((static_cast<float>(row) * kGridSpacing) - kGridOffset);
            instancePositions[instanceIndex] = {x, y, 0.0f};
        }
    }
    return create_gpu_buffer(engine, instancePositions.size() * sizeof(glm::vec3), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, instancePositions.data(),
                             engine->instanceBuffer, engine->instanceBufferAllocation, "Instance_Offsets_Buffer");
}

bool create_billboard_instance_buffer(VulkanEngine* engine, const std::vector<glm::vec3>& instancePositions) {
    const size_t instanceCount = instancePositions.size();
    engine->core.billboardInstances.resize(instanceCount);
    for (size_t i = 0; i < instanceCount; ++i) {
        engine->core.billboardInstances[i] = {instancePositions[i], static_cast<int>(i)};
    }

    VkBufferCreateInfo billboardIn{};
    billboardIn.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    billboardIn.size = instanceCount * sizeof(BillboardInstance);
    billboardIn.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VmaAllocationCreateInfo billboardAl{};
    billboardAl.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    if (vmaCreateBuffer(engine->allocator, &billboardIn, &billboardAl, &engine->billboardBuffer, &engine->billboardAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->billboardBuffer, VK_OBJECT_TYPE_BUFFER, "Billboard_Instance_Buffer");
    return vmaMapMemory(engine->allocator, engine->billboardAllocation, &engine->billboardMapped) == VK_SUCCESS;
}

bool create_material_ssbo(VulkanEngine* engine) {
    std::vector<MaterialGpu> materials;
    if (!load_legacy_materials_for_grid(materials, kMaterialInstanceCount)) {
        return false;
    }
    return create_gpu_buffer(engine, materials.size() * sizeof(MaterialGpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, materials.data(), engine->materialBuffer,
                             engine->materialBufferAllocation, "PBR_Materials_SSBO");
}

bool create_global_uniform_buffer(VulkanEngine* engine) {
    VkBufferCreateInfo uboIn{};
    uboIn.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    uboIn.size = sizeof(UBOData);
    uboIn.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VmaAllocationCreateInfo uboAl{};
    uboAl.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    if (vmaCreateBuffer(engine->allocator, &uboIn, &uboAl, &engine->uniformBuffer, &engine->uniformBufferAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->uniformBuffer, VK_OBJECT_TYPE_BUFFER, "Global_MVP_UBO");
    return vmaMapMemory(engine->allocator, engine->uniformBufferAllocation, &engine->uniformBufferMapped) == VK_SUCCESS;
}

bool init_pipeline(VulkanEngine* engine) {
    // 1. Load Main and Skybox Shaders
    VkShaderModule vsm = load_shader(engine->device, "shaders/vert.spv");
    VkShaderModule fsm = load_shader(engine->device, "shaders/frag.spv");
    VkShaderModule skyboxVsm = load_shader(engine->device, "shaders/skybox_vert.spv");
    VkShaderModule skyboxFsm = load_shader(engine->device, "shaders/skybox_frag.spv");

    if (vsm == VK_NULL_HANDLE || fsm == VK_NULL_HANDLE || skyboxVsm == VK_NULL_HANDLE || skyboxFsm == VK_NULL_HANDLE) {
        if (vsm)
            vkDestroyShaderModule(engine->device, vsm, nullptr);
        if (fsm)
            vkDestroyShaderModule(engine->device, fsm, nullptr);
        if (skyboxVsm)
            vkDestroyShaderModule(engine->device, skyboxVsm, nullptr);
        if (skyboxFsm)
            vkDestroyShaderModule(engine->device, skyboxFsm, nullptr);
        return false;
    }

    vk_set_object_name(engine->device, (uint64_t)vsm, VK_OBJECT_TYPE_SHADER_MODULE, "Icosphere_Vertex_Shader");
    vk_set_object_name(engine->device, (uint64_t)fsm, VK_OBJECT_TYPE_SHADER_MODULE, "Icosphere_Fragment_Shader");
    vk_set_object_name(engine->device, (uint64_t)skyboxVsm, VK_OBJECT_TYPE_SHADER_MODULE, "Skybox_Vertex_Shader");
    vk_set_object_name(engine->device, (uint64_t)skyboxFsm, VK_OBJECT_TYPE_SHADER_MODULE, "Skybox_Fragment_Shader");

    // 2. Setup Pipeline Layout
    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &engine->descriptorSetLayout;
    if (vkCreatePipelineLayout(engine->device, &plInfo, nullptr, &engine->pipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(engine->device, vsm, nullptr);
        vkDestroyShaderModule(engine->device, fsm, nullptr);
        vkDestroyShaderModule(engine->device, skyboxVsm, nullptr);
        vkDestroyShaderModule(engine->device, skyboxFsm, nullptr);
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->pipelineLayout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "Main_Pipeline_Layout");

    // 3. Initialize Common Pipeline State
    PipelineCommonState common;
    init_common_pipeline_state(engine, common);

    // 4. Create specialized pipelines
    bool success = true;
    success &= create_main_graphics_pipeline(engine, common, vsm, fsm);
    success &= create_skybox_pipeline(engine, common, skyboxVsm, skyboxFsm);

    // Setup Billboard Vertex Input (reused by debug pipelines)
    VkVertexInputBindingDescription bBindings[1] = {};
    bBindings[0].binding = 1;
    bBindings[0].stride = sizeof(BillboardInstance);
    bBindings[0].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription bAttrs[2] = {};
    bAttrs[0].location = 2;
    bAttrs[0].binding = 1;
    bAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    bAttrs[0].offset = offsetof(BillboardInstance, pos);
    bAttrs[1].location = 3;
    bAttrs[1].binding = 1;
    bAttrs[1].format = VK_FORMAT_R32_SINT;
    bAttrs[1].offset = offsetof(BillboardInstance, materialIdx);

    VkPipelineVertexInputStateCreateInfo bVi{};
    bVi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    bVi.vertexBindingDescriptionCount = 1;
    bVi.pVertexBindingDescriptions = bBindings;
    bVi.vertexAttributeDescriptionCount = 2;
    bVi.pVertexAttributeDescriptions = bAttrs;

    success &= create_billboard_pipeline(engine, common, &bVi);
    success &= create_wireframe_pipeline(engine, common);
    success &= create_debug_pipelines(engine, common, &bVi);

    // Cleanup
    vkDestroyShaderModule(engine->device, vsm, nullptr);
    vkDestroyShaderModule(engine->device, fsm, nullptr);
    vkDestroyShaderModule(engine->device, skyboxVsm, nullptr);
    vkDestroyShaderModule(engine->device, skyboxFsm, nullptr);

    return success;
}

bool init_buffers(VulkanEngine* engine) {
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
    vk_set_object_name(engine->device, (uint64_t)engine->commandPool, VK_OBJECT_TYPE_COMMAND_POOL, "Main_Command_Pool");

    std::vector<glm::vec3> instancePositions;
    bool success = true;
    success &= create_icosphere_buffers(engine, sphere);
    success &= create_instance_grid_buffers(engine, instancePositions);
    success &= create_billboard_instance_buffer(engine, instancePositions);
    success &= create_material_ssbo(engine);
    success &= create_global_uniform_buffer(engine);

    return success;
}

bool init_descriptor_pool_and_sets(VulkanEngine* engine) {
    VkDescriptorPoolSize sizes[3] = {};
    sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = 1;
    sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[1].descriptorCount = 4; // environment, irradiance, prefiltered, brdfLut
    sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    sizes[2].descriptorCount = 1;

    VkDescriptorPoolCreateInfo pIn{};
    pIn.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pIn.maxSets = 1;
    pIn.poolSizeCount = 3;
    pIn.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(engine->device, &pIn, nullptr, &engine->descriptorPool) != VK_SUCCESS) {
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->descriptorPool, VK_OBJECT_TYPE_DESCRIPTOR_POOL, "Global_Descriptor_Pool");

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = engine->descriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &engine->descriptorSetLayout;
    if (vkAllocateDescriptorSets(engine->device, &ai, &engine->descriptorSet) != VK_SUCCESS) {
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->descriptorSet, VK_OBJECT_TYPE_DESCRIPTOR_SET, "Global_Descriptor_Set");

    VkDescriptorBufferInfo bi{};
    bi.buffer = engine->uniformBuffer;
    bi.offset = 0;
    bi.range = sizeof(UBOData);

    VkDescriptorBufferInfo materialBufferInfo{};
    materialBufferInfo.buffer = engine->materialBuffer;
    materialBufferInfo.offset = 0;
    materialBufferInfo.range = VK_WHOLE_SIZE;

    if (engine->envHdrImageView == VK_NULL_HANDLE || engine->envHdrSampler == VK_NULL_HANDLE) {
        LOG_WARNING("engine", "init_descriptor_pool_and_sets: environment HDR resources not ready, initial descriptor update will use fallbacks.");
    }

    VkDescriptorImageInfo envInfo{};
    envInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    envInfo.imageView = engine->envHdrImageView ? engine->envHdrImageView : engine->ibl.irradianceMapView; // Fallback to IR if Env NULL
    envInfo.sampler = engine->envHdrSampler ? engine->envHdrSampler : engine->ibl.irradianceSampler;

    // Provide dummy or real image info for IBL maps (initially they might be empty, but they are created in init_ibl)
    // We assume init_ibl has been called before this function.
    VkDescriptorImageInfo irrInfo{};
    irrInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    irrInfo.imageView = engine->ibl.irradianceMapView ? engine->ibl.irradianceMapView : engine->envHdrImageView;
    irrInfo.sampler = engine->ibl.irradianceSampler ? engine->ibl.irradianceSampler : engine->envHdrSampler;

    VkDescriptorImageInfo prefInfo{};
    prefInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    prefInfo.imageView = engine->ibl.prefilteredMapView ? engine->ibl.prefilteredMapView : engine->envHdrImageView;
    prefInfo.sampler = engine->ibl.prefilteredSampler ? engine->ibl.prefilteredSampler : engine->envHdrSampler;

    VkDescriptorImageInfo lutInfo{};
    lutInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    lutInfo.imageView = engine->ibl.brdfLutView ? engine->ibl.brdfLutView : engine->envHdrImageView;
    lutInfo.sampler = engine->ibl.brdfLutSampler ? engine->ibl.brdfLutSampler : engine->envHdrSampler;

    // Safety: ensure no NULL handles are passed to vkUpdateDescriptorSets for required bindings
    if (envInfo.imageView == VK_NULL_HANDLE || irrInfo.imageView == VK_NULL_HANDLE || prefInfo.imageView == VK_NULL_HANDLE ||
        lutInfo.imageView == VK_NULL_HANDLE) {
        LOG_ERROR("engine", "init_descriptor_pool_and_sets: one or more required images are NULL (spec violation). Skipping initial update.");
        return true; // We'll update later in vk_init_environment_texture
    }

    VkWriteDescriptorSet writes[6] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = engine->descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &bi;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = engine->descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &envInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = engine->descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &irrInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = engine->descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].pImageInfo = &prefInfo;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = engine->descriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].pImageInfo = &lutInfo;

    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = engine->descriptorSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorCount = 1;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].pBufferInfo = &materialBufferInfo;

    vkUpdateDescriptorSets(engine->device, 6, writes, 0, nullptr);
    return true;
}

bool init_commands_and_sync(VulkanEngine* engine) {
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
    vk_set_object_name(engine->device, (uint64_t)engine->imageAvailableSemaphore, VK_OBJECT_TYPE_SEMAPHORE, "Image_Available_Semaphore");
    if (vkCreateSemaphore(engine->device, &si, nullptr, &engine->renderFinishedSemaphore) != VK_SUCCESS) {
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->renderFinishedSemaphore, VK_OBJECT_TYPE_SEMAPHORE, "Render_Finished_Semaphore");
    if (vkCreateFence(engine->device, &fi, nullptr, &engine->inFlightFence) != VK_SUCCESS) {
        return false;
    }
    vk_set_object_name(engine->device, (uint64_t)engine->inFlightFence, VK_OBJECT_TYPE_FENCE, "Main_Render_Fence");
    return true;
}

} // namespace

// --- FONCTIONS PUBLIQUES (vk_engine_init) ---

bool vk_recreate_swapchain(VulkanEngine* engine) {
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

bool vk_init_vulkan_engine(VulkanEngine* engine) {
    SVK_TRACY_ZONE_SCOPED("vk_init_vulkan_engine");
    LOG_INFO("app", "Starting engine initialization...");
    LOG_INFO("vulkan", "Vulkan Debug Callback initialized (High Sensitivity)");
    LOG_INFO("app", "init_core...");
    if (!init_core(engine)) {
        LOG_ERROR("app", "init_core failed");
        return false;
    }
    LOG_INFO("app", "init_allocator...");
    if (!init_allocator(engine)) {
        LOG_ERROR("app", "init_allocator failed");
        return false;
    }
    LOG_INFO("app", "init_swapchain...");
    if (!init_swapchain(engine)) {
        LOG_ERROR("app", "init_swapchain failed");
        return false;
    }
    LOG_INFO("app", "init_render_pass...");
    if (!init_render_pass(engine)) {
        LOG_ERROR("app", "init_render_pass failed");
        return false;
    }
    LOG_INFO("app", "init_descriptor_layout...");
    if (!init_descriptor_layout(engine)) {
        LOG_ERROR("app", "init_descriptor_layout failed");
        return false;
    }
    LOG_INFO("app", "init_pipeline...");
    if (!init_pipeline(engine)) {
        LOG_ERROR("app", "init_pipeline failed");
        return false;
    }
    LOG_INFO("app", "init_buffers...");
    if (!init_buffers(engine)) {
        LOG_ERROR("app", "init_buffers failed");
        return false;
    }
    LOG_INFO("app", "vk_init_environment_catalog...");
    if (!vk_init_environment_catalog(engine)) {
        LOG_ERROR("app", "vk_init_environment_catalog failed");
        return false;
    }
    if (!init_ibl(engine)) {
        LOG_ERROR("app", "init_ibl failed");
        return false;
    }

    LOG_INFO("app", "Initializing commands and sync objects...");
    if (!init_commands_and_sync(engine)) {
        LOG_ERROR("app", "init_commands_and_sync failed");
        return false;
    }

    if (!tracy_vk_context_init(engine)) {
        LOG_ERROR("tracy", "tracy_vk_context_init failed");
        return false;
    }

    LOG_INFO("app", "Initializing environment texture (triggers bake)...");
    if (!vk_init_environment_texture(engine)) {
        LOG_ERROR("app", "vk_init_environment_texture failed");
        return false;
    }

    LOG_INFO("app", "Initializing descriptor pool and sets (commandPool=%p)...", (void*)engine->commandPool);
    if (!init_descriptor_pool_and_sets(engine)) {
        LOG_ERROR("app", "init_descriptor_pool_and_sets failed");
        return false;
    }

    LOG_INFO("app", "Initialization complete.");
    core_engine_init(&engine->core);

    if (engine->useNullRHI) {
        engine->rhi = new NullRHI();
    } else {
        engine->rhi = new VulkanRHI(engine);
    }
    engine->rhi->Init();

    engine->hdrIoThreadRunning = false;
    engine->hdrLoadInFlight = false;
    engine->pendingHdrIndex = -1;

    glfwSetInputMode(engine->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Some values still depend on window layout
    glfwGetWindowPos(engine->window, &engine->core.windowedPosX, &engine->core.windowedPosY);
    glfwGetWindowSize(engine->window, &engine->core.windowedWidth, &engine->core.windowedHeight);
    if (!vk_start_hdr_io_thread(engine)) {
        vk_cleanup_vulkan_engine(engine);
        return false;
    }
    LOG_INFO("engine", "Vulkan initialise avec succes !");
    LOG_INFO("postprocess", "Default Exposure: %.2f", engine->core.exposure);
    LOG_INFO("postprocess", "Default IBL Intensity: %.2f (Scale: %.2f)", engine->core.iblIntensity, engine->core.iblDebugScale);
    LOG_INFO("postprocess", "Default Tonemapper: Filmic ACES (DISABLED by default for Legacy OGL-ISO parity)");
    LOG_INFO("postprocess", "Default Color Grading: Sat=%.2f, Contrast=%.2f, Gamma=%.2f, Gain=%.2f, Offset=%.2f", engine->core.saturation,
             engine->core.contrast, engine->core.gamma, engine->core.gain, engine->core.offset);
    LOG_INFO("postprocess", "Default White Balance: Temp=%.1f, Tint=%.2f", engine->core.wbTemp, engine->core.wbTint);
    return true;
}

void vk_cleanup_vulkan_engine(VulkanEngine* engine) {
    SVK_TRACY_ZONE_SCOPED("vk_cleanup_vulkan_engine");
    vk_stop_hdr_io_thread(engine);
    LOG_INFO("async", "Async loader destroyed");

    if (engine->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(engine->device);
    }

    tracy_vk_context_destroy(engine);

    cleanup_sync_objects(engine);
    cleanup_descriptor_resources(engine);
    cleanup_ibl(engine);
    cleanup_buffer_resources(engine);
    LOG_INFO("material", "Material library memory freed successfully");
    cleanup_render_resources(engine);
    cleanup_core_resources(engine);
    LOG_INFO("postprocess", "Post-processing cleaned up");
    LOG_INFO("perf", "Performance mode cleaned up");
    if (engine->rhi) {
        delete engine->rhi;
        engine->rhi = nullptr;
    }
    glfwTerminate();
}
