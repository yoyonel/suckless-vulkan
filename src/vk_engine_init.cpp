

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
    if (engine->depthImage.is_valid()) {
    }
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

void cleanup_descriptor_resources(VulkanEngine* /*engine*/) {}

void cleanup_buffer_resources(VulkanEngine* engine) {
    engine->appState->rhi->UnmapBuffer(engine->uniformBuffer);

    if (engine->transformBufferMapped) {
        engine->appState->rhi->UnmapBuffer(engine->transformBuffer);
    }

    vk_cleanup_environment_resources(engine);
}

void cleanup_render_resources(VulkanEngine* engine) {
    cleanup_swapchain_dependent_resources(engine);
    destroy_device_handle(engine->device, engine->commandPool, vkDestroyCommandPool);
}

void cleanup_raii_resources(VulkanEngine* engine) {
    engine->globalDescriptorPool.Reset();
    engine->ibl.computeDescriptorPool.Reset();

    engine->depthImage.Reset();
    engine->envHdrImage.Reset();
    engine->envHdrSampler.Reset();

    engine->ibl.irradianceMap.Reset();
    engine->ibl.irradianceSampler.Reset();
    engine->ibl.prefilteredMap.Reset();
    engine->ibl.prefilteredSampler.Reset();
    engine->ibl.brdfLut.Reset();
    engine->ibl.brdfLutSampler.Reset();

    engine->graphicsPipeline.Reset();
    engine->billboardPipeline.Reset();
    engine->wireframePipeline.Reset();
    engine->debugLinePipeline.Reset();
    engine->debugTrianglePipeline.Reset();
    engine->skyboxPipeline.Reset();

    engine->ibl.irmapPipeline.Reset();
    engine->ibl.spmapPipeline.Reset();
    engine->ibl.brdfLutPipeline.Reset();
    engine->ibl.lum1Pipeline.Reset();
    engine->ibl.lum2Pipeline.Reset();

    engine->pipelineLayout.Reset();
    engine->debugPipelineLayout.Reset();
    engine->ibl.iblPipelineLayout.Reset();
    engine->ibl.lum1PipelineLayout.Reset();
    engine->ibl.lum2PipelineLayout.Reset();

    engine->globalDescriptorLayout.Reset();
    engine->ibl.iblDescriptorSetLayout.Reset();
    engine->ibl.lum1DescriptorSetLayout.Reset();
    engine->ibl.lum2DescriptorSetLayout.Reset();

    engine->vertexBuffer.Reset();
    engine->indexBuffer.Reset();
    engine->transformBuffer.Reset();
    engine->materialBuffer.Reset();
    engine->uniformBuffer.Reset();
    engine->billboardBuffer.Reset();
    engine->billboardPosSSBO.Reset();
    engine->billboardMatSSBO.Reset();
}

void cleanup_core_resources(VulkanEngine* engine) {
    cleanup_raii_resources(engine);
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
}

bool init_core(VulkanEngine* engine) {
    if (engine->appState->window == nullptr) {
        return false;
    }

    glfwSetWindowUserPointer(engine->appState->window, engine);
    glfwSetCursorPosCallback(engine->appState->window, vk_mouse_callback);
    glfwSetScrollCallback(engine->appState->window, vk_scroll_callback);

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
    if (glfwCreateWindowSurface(engine->instance, engine->appState->window, NULL, &engine->surface) != VK_SUCCESS)
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
    const VkPresentModeKHR presentMode = choose_present_mode(presentModes, engine->appState->core.vsync);
    engine->swapchainImageFormat = surfaceFormat.format;
    engine->swapchainExtent = choose_swapchain_extent(engine->appState->window, capabilities);

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

    engine->depthImage.Reset(engine->appState->rhi,
                             engine->appState->rhi->CreateTexture(engine->swapchainExtent.width, engine->swapchainExtent.height, TextureFormat::Depth,
                                                                  TextureUsage::DepthAttachment, 1, "Depth_Buffer_Image"));

    return engine->depthImage.is_valid();
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
        VkImageView depthImageView = ((VulkanRHI*)engine->appState->rhi)->GetVkImageView(engine->depthImage);
        VkImageView fbAtt[] = {engine->swapchainImageViews[i], depthImageView};
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
    std::vector<DescriptorSetLayoutBinding> bindings = {
        {0, DescriptorType::UniformBuffer, 1, ShaderStage::AllGraphics},     {1, DescriptorType::CombinedImageSampler, 1, ShaderStage::Fragment},
        {2, DescriptorType::CombinedImageSampler, 1, ShaderStage::Fragment}, {3, DescriptorType::CombinedImageSampler, 1, ShaderStage::Fragment},
        {4, DescriptorType::CombinedImageSampler, 1, ShaderStage::Fragment}, {5, DescriptorType::StorageBuffer, 1, ShaderStage::Fragment},
        {6, DescriptorType::StorageBuffer, 1, ShaderStage::Vertex},          {7, DescriptorType::StorageBuffer, 1, ShaderStage::Vertex},
        {8, DescriptorType::StorageBuffer, 1, ShaderStage::Vertex},          {9, DescriptorType::StorageBuffer, 1, ShaderStage::Vertex}};
    DescriptorLayoutDesc desc{bindings.data(), static_cast<uint32_t>(bindings.size())};
    engine->globalDescriptorLayout.Reset(engine->appState->rhi, engine->appState->rhi->CreateDescriptorLayout(desc, "Global_DescriptorSetLayout"));
    return engine->globalDescriptorLayout.is_valid();
}

std::vector<uint32_t> load_shader(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f)
        return {};
    fseek(f, 0, SEEK_END);
    size_t size = static_cast<size_t>(ftell(f));
    fseek(f, 0, SEEK_SET);
    std::vector<uint32_t> buffer(size / 4);
    if (fread(buffer.data(), 1, size, f) != size) {
        fclose(f);
        return {};
    }
    fclose(f);
    return buffer;
}

bool create_main_graphics_pipeline(VulkanEngine* engine, const std::vector<uint32_t>& vsm, const std::vector<uint32_t>& fsm) {
    VertexInputBinding bindings[] = {{0, sizeof(Vertex), false}};
    VertexInputAttribute attrs[] = {{0, 0, VertexFormat::Float3, offsetof(Vertex, position)}, {1, 0, VertexFormat::Float3, offsetof(Vertex, color)}};
    GraphicsPipelineDesc desc{};
    desc.layout = engine->pipelineLayout;
    desc.renderPass = engine->renderPass;
    desc.vertexShaderCode = vsm.data();
    desc.vertexShaderSize = vsm.size() * 4;
    desc.fragmentShaderCode = fsm.data();
    desc.fragmentShaderSize = fsm.size() * 4;
    desc.vertexBindings = bindings;
    desc.vertexBindingCount = 1;
    desc.vertexAttributes = attrs;
    desc.vertexAttributeCount = 2;
    desc.topology = Topology::TriangleList;
    desc.debugName = "Main_Graphics_Pipeline";
    engine->graphicsPipeline.Reset(engine->appState->rhi, engine->appState->rhi->CreateGraphicsPipeline(desc));
    return engine->graphicsPipeline.is_valid();
}

bool create_skybox_pipeline(VulkanEngine* engine, const std::vector<uint32_t>& vsm, const std::vector<uint32_t>& fsm) {
    GraphicsPipelineDesc desc{};
    desc.layout = engine->pipelineLayout;
    desc.renderPass = engine->renderPass;
    desc.vertexShaderCode = vsm.data();
    desc.vertexShaderSize = vsm.size() * 4;
    desc.fragmentShaderCode = fsm.data();
    desc.fragmentShaderSize = fsm.size() * 4;
    desc.cullMode = CullMode::None;
    desc.depthWriteEnable = false;
    desc.depthCompareOp = CompareOp::LessOrEqual;
    desc.debugName = "Skybox_Graphics_Pipeline";
    engine->skyboxPipeline.Reset(engine->appState->rhi, engine->appState->rhi->CreateGraphicsPipeline(desc));
    return engine->skyboxPipeline.is_valid();
}

bool create_billboard_pipeline(VulkanEngine* engine) {
    auto bvm = load_shader("shaders/billboard_vert.spv");
    auto bfm = load_shader("shaders/billboard_frag.spv");
    if (bvm.empty() || bfm.empty())
        return false;

    GraphicsPipelineDesc desc{};
    desc.layout = engine->pipelineLayout;
    desc.renderPass = engine->renderPass;
    desc.vertexShaderCode = bvm.data();
    desc.vertexShaderSize = bvm.size() * 4;
    desc.fragmentShaderCode = bfm.data();
    desc.fragmentShaderSize = bfm.size() * 4;
    desc.cullMode = CullMode::None;
    desc.depthWriteEnable = false;
    desc.colorBlendEnable = true;
    desc.vertexBindings = nullptr;
    desc.vertexBindingCount = 0;
    desc.vertexAttributes = nullptr;
    desc.vertexAttributeCount = 0;
    desc.debugName = "Billboard_Graphics_Pipeline";
    engine->billboardPipeline.Reset(engine->appState->rhi, engine->appState->rhi->CreateGraphicsPipeline(desc));
    return engine->billboardPipeline.is_valid();
}

bool create_wireframe_pipeline(VulkanEngine* engine, const std::vector<uint32_t>& vsm, const std::vector<uint32_t>& fsm) {
    VertexInputBinding bindings[] = {{0, sizeof(Vertex), false}};
    VertexInputAttribute attrs[] = {{0, 0, VertexFormat::Float3, offsetof(Vertex, position)}, {1, 0, VertexFormat::Float3, offsetof(Vertex, color)}};
    GraphicsPipelineDesc desc{};
    desc.layout = engine->pipelineLayout;
    desc.renderPass = engine->renderPass;
    desc.vertexShaderCode = vsm.data();
    desc.vertexShaderSize = vsm.size() * 4;
    desc.fragmentShaderCode = fsm.data();
    desc.fragmentShaderSize = fsm.size() * 4;
    desc.vertexBindings = bindings;
    desc.vertexBindingCount = 1;
    desc.vertexAttributes = attrs;
    desc.vertexAttributeCount = 2;
    desc.polygonMode = PolygonMode::Line;
    desc.debugName = "Wireframe_Pipeline";
    engine->wireframePipeline.Reset(engine->appState->rhi, engine->appState->rhi->CreateGraphicsPipeline(desc));
    return engine->wireframePipeline.is_valid();
}

bool create_debug_pipelines(VulkanEngine* engine) {
    auto dvm = load_shader("shaders/debug_vert.spv");
    auto dfm = load_shader("shaders/debug_frag.spv");
    if (dvm.empty() || dfm.empty())
        return false;

    PushConstantRange dPushRange{ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(DebugPushConstant)};
    DescriptorLayoutHandle d[] = {engine->globalDescriptorLayout};
    PipelineLayoutDesc plDesc{d, 1, &dPushRange, 1};
    engine->debugPipelineLayout.Reset(engine->appState->rhi, engine->appState->rhi->CreatePipelineLayout(plDesc, "Debug_PipelineLayout"));

    GraphicsPipelineDesc desc{};
    desc.layout = engine->debugPipelineLayout;
    desc.renderPass = engine->renderPass;
    desc.vertexShaderCode = dvm.data();
    desc.vertexShaderSize = dvm.size() * 4;
    desc.fragmentShaderCode = dfm.data();
    desc.fragmentShaderSize = dfm.size() * 4;
    desc.vertexBindings = nullptr;
    desc.vertexBindingCount = 0;
    desc.vertexAttributes = nullptr;
    desc.vertexAttributeCount = 0;

    desc.topology = Topology::LineList;
    desc.polygonMode = PolygonMode::Line;
    desc.cullMode = CullMode::None;
    desc.depthWriteEnable = false;
    desc.debugName = "Debug_Line_Pipeline";
    engine->debugLinePipeline.Reset(engine->appState->rhi, engine->appState->rhi->CreateGraphicsPipeline(desc));

    desc.topology = Topology::TriangleList;
    desc.polygonMode = PolygonMode::Fill;
    desc.colorBlendEnable = true;
    desc.debugName = "Debug_Triangle_Pipeline";
    engine->debugTrianglePipeline.Reset(engine->appState->rhi, engine->appState->rhi->CreateGraphicsPipeline(desc));
    return engine->debugLinePipeline.is_valid() && engine->debugTrianglePipeline.is_valid();
}

bool init_pipeline(VulkanEngine* engine) {
    DescriptorLayoutHandle d[] = {engine->globalDescriptorLayout};
    PipelineLayoutDesc plDesc{d, 1, nullptr, 0};
    engine->pipelineLayout.Reset(engine->appState->rhi, engine->appState->rhi->CreatePipelineLayout(plDesc, "Main_Pipeline_Layout"));

    auto vsm = load_shader("shaders/vert.spv");
    auto fsm = load_shader("shaders/frag.spv");
    auto skyboxVsm = load_shader("shaders/skybox_vert.spv");
    auto skyboxFsm = load_shader("shaders/skybox_frag.spv");
    if (vsm.empty() || fsm.empty() || skyboxVsm.empty() || skyboxFsm.empty())
        return false;

    bool success = true;
    success &= create_main_graphics_pipeline(engine, vsm, fsm);
    success &= create_skybox_pipeline(engine, skyboxVsm, skyboxFsm);
    success &= create_wireframe_pipeline(engine, vsm, fsm);
    success &= create_billboard_pipeline(engine);
    success &= create_debug_pipelines(engine);

    return success;
}

bool create_icosphere_buffers(VulkanEngine* engine, const Icosphere& sphere) {
    engine->vertexBuffer.Reset(engine->appState->rhi, engine->appState->rhi->CreateBuffer(sphere.vertices.size() * sizeof(Vertex), BufferUsage::Vertex,
                                                                                          sphere.vertices.data(), "Icosphere_Vertex_Buffer"));
    if (!engine->vertexBuffer.is_valid())
        return false;

    engine->indexBuffer.Reset(engine->appState->rhi, engine->appState->rhi->CreateBuffer(sphere.indices.size() * sizeof(uint32_t), BufferUsage::Index,
                                                                                         sphere.indices.data(), "Icosphere_Index_Buffer"));
    return engine->indexBuffer.is_valid();
}

bool create_instance_grid_buffers(VulkanEngine* engine, std::vector<glm::vec3>& instancePositions) {
    const size_t instanceCount = kMaterialInstanceCount;
    engine->appState->core.scene.instanceCount = static_cast<uint32_t>(instanceCount);
    engine->appState->core.scene.instancePositions =
        static_cast<glm::vec3*>(arena_alloc(&engine->appState->core.scene.arena, instanceCount * sizeof(glm::vec3), 64));
    instancePositions.resize(instanceCount);

    for (uint32_t row = 0; row < kGridSize; ++row) {
        for (uint32_t col = 0; col < kGridSize; ++col) {
            const size_t instanceIndex = (static_cast<size_t>(row) * static_cast<size_t>(kGridSize)) + static_cast<size_t>(col);
            const float x = (static_cast<float>(col) * kGridSpacing) - kGridOffset;
            const float y = -((static_cast<float>(row) * kGridSpacing) - kGridOffset);
            engine->appState->core.scene.instancePositions[instanceIndex] = {x, y, 0.0f};
            instancePositions[instanceIndex] = {x, y, 0.0f};
        }
    }
    engine->transformBuffer.Reset(engine->appState->rhi,
                                  engine->appState->rhi->CreateBuffer(instanceCount * sizeof(glm::mat4), BufferUsage::Storage, nullptr, "Flat_Transform_SSBO"));
    if (engine->transformBuffer.is_valid()) {
        engine->transformBufferMapped = engine->appState->rhi->MapBuffer(engine->transformBuffer);
    }
    return engine->transformBuffer.is_valid();
}

bool create_billboard_instance_buffer(VulkanEngine* engine, const std::vector<glm::vec3>& instancePositions) {
    const size_t instanceCount = instancePositions.size();

    BillboardSoA* soa = &engine->appState->core.scene.billboardSoA;
    soa->count = static_cast<int>(instanceCount);
    soa->capacity = static_cast<int>(instanceCount);
    soa->pos = static_cast<glm::vec4*>(arena_alloc(&engine->appState->core.scene.arena, instanceCount * sizeof(glm::vec4), 64));
    soa->materialIdx = static_cast<int*>(arena_alloc(&engine->appState->core.scene.arena, instanceCount * sizeof(int), 64));

    for (size_t i = 0; i < instanceCount; ++i) {
        soa->pos[i] = glm::vec4(instancePositions[i], 1.0f);
        soa->materialIdx[i] = static_cast<int>(i);
    }

    engine->billboardBuffer.Reset(engine->appState->rhi,
                                  engine->appState->rhi->CreateBuffer(instanceCount * sizeof(uint32_t), BufferUsage::Storage, nullptr, "Billboard_Index_SSBO"));
    engine->billboardPosSSBO.Reset(
        engine->appState->rhi, engine->appState->rhi->CreateBuffer(instanceCount * sizeof(glm::vec4), BufferUsage::Storage, soa->pos, "Billboard_Pos_SSBO"));
    engine->billboardMatSSBO.Reset(
        engine->appState->rhi, engine->appState->rhi->CreateBuffer(instanceCount * sizeof(int), BufferUsage::Storage, soa->materialIdx, "Billboard_Mat_SSBO"));

    return engine->billboardBuffer.is_valid() && engine->billboardPosSSBO.is_valid() && engine->billboardMatSSBO.is_valid();
}

bool create_material_ssbo(VulkanEngine* engine) {
    std::vector<MaterialGpu> materials;
    if (!load_legacy_materials_for_grid(materials, kMaterialInstanceCount)) {
        return false;
    }
    if (materials.empty()) {
        return true;
    }
    engine->materialBuffer.Reset(engine->appState->rhi, engine->appState->rhi->CreateBuffer(materials.size() * sizeof(MaterialGpu), BufferUsage::Storage,
                                                                                            materials.data(), "PBR_Materials_SSBO"));
    return engine->materialBuffer.is_valid();
}

bool create_global_uniform_buffer(VulkanEngine* engine) {
    engine->uniformBuffer.Reset(engine->appState->rhi, engine->appState->rhi->CreateBuffer(sizeof(UBOData), BufferUsage::Uniform, nullptr, "Global_MVP_UBO"));
    if (!engine->uniformBuffer.is_valid())
        return false;
    engine->uniformBufferMapped = engine->appState->rhi->MapBuffer(engine->uniformBuffer);
    return engine->uniformBufferMapped != nullptr;
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
    DescriptorPoolSize sizes[3] = {{DescriptorType::UniformBuffer, 1}, {DescriptorType::CombinedImageSampler, 4}, {DescriptorType::StorageBuffer, 5}};
    DescriptorPoolDesc desc{sizes, 3, 1};
    engine->globalDescriptorPool.Reset(engine->appState->rhi, engine->appState->rhi->CreateDescriptorPool(desc, "Global_Descriptor_Pool"));
    if (!engine->globalDescriptorPool.is_valid()) {
        return false;
    }

    DescriptorSetAllocateDesc ai{};
    ai.pool = engine->globalDescriptorPool;
    ai.setCount = 1;
    DescriptorLayoutHandle layoutHandle = engine->globalDescriptorLayout;
    ai.layouts = &layoutHandle;
    if (!engine->appState->rhi->AllocateDescriptorSets(ai, &engine->descriptorSet)) {
        return false;
    }
    VkDescriptorSet vkSet = ((VulkanRHI*)engine->appState->rhi)->GetVkDescriptorSet(engine->descriptorSet);
    vk_set_object_name(engine->device, (uint64_t)vkSet, VK_OBJECT_TYPE_DESCRIPTOR_SET, "Global_Descriptor_Set");

    DescriptorBufferInfo bi{};
    bi.buffer = engine->uniformBuffer;
    bi.offset = 0;
    bi.range = sizeof(UBOData);

    DescriptorBufferInfo materialBufferInfo{};
    materialBufferInfo.buffer = engine->materialBuffer;
    materialBufferInfo.offset = 0;
    materialBufferInfo.range = VK_WHOLE_SIZE;

    DescriptorImageInfo envInfo{};
    TextureHandle tex = engine->envHdrImage.is_valid() ? engine->envHdrImage : INVALID_HANDLE;
    if (tex == INVALID_HANDLE)
        tex = engine->ibl.irradianceMap.is_valid() ? engine->ibl.irradianceMap : INVALID_HANDLE;
    envInfo.texture = tex;
    envInfo.imageView = INVALID_HANDLE;
    SamplerHandle samp = engine->envHdrSampler.is_valid() ? engine->envHdrSampler : INVALID_HANDLE;
    if (samp == INVALID_HANDLE)
        samp = engine->ibl.irradianceSampler.is_valid() ? engine->ibl.irradianceSampler : INVALID_HANDLE;
    envInfo.sampler = samp;
    envInfo.imageView = INVALID_HANDLE;
    envInfo.imageLayout = TextureLayout::ShaderReadOnlyOptimal;

    DescriptorImageInfo irrInfo{};
    irrInfo.texture = engine->ibl.irradianceMap.is_valid() ? engine->ibl.irradianceMap : engine->envHdrImage;
    irrInfo.sampler = engine->ibl.irradianceSampler.is_valid() ? engine->ibl.irradianceSampler : engine->envHdrSampler;
    irrInfo.imageView = INVALID_HANDLE;
    irrInfo.imageLayout = TextureLayout::ShaderReadOnlyOptimal;

    DescriptorImageInfo prefInfo{};
    prefInfo.texture = engine->ibl.prefilteredMap.is_valid() ? engine->ibl.prefilteredMap : engine->envHdrImage;
    prefInfo.sampler = engine->ibl.prefilteredSampler.is_valid() ? engine->ibl.prefilteredSampler : engine->envHdrSampler;
    prefInfo.imageView = INVALID_HANDLE;
    prefInfo.imageLayout = TextureLayout::ShaderReadOnlyOptimal;

    DescriptorImageInfo lutInfo{};
    lutInfo.texture = engine->ibl.brdfLut.is_valid() ? engine->ibl.brdfLut : engine->envHdrImage;
    lutInfo.sampler = engine->ibl.brdfLutSampler.is_valid() ? engine->ibl.brdfLutSampler : engine->envHdrSampler;
    lutInfo.imageView = INVALID_HANDLE;
    lutInfo.imageLayout = TextureLayout::ShaderReadOnlyOptimal;

    // Safety: ensure no invalid handles are passed for required bindings
    if (envInfo.texture == INVALID_HANDLE || irrInfo.texture == INVALID_HANDLE || prefInfo.texture == INVALID_HANDLE || lutInfo.texture == INVALID_HANDLE) {
        LOG_ERROR("engine", "init_descriptor_pool_and_sets: one or more required images are INVALID (spec violation). Skipping initial update.");
        return true; // We'll update later in vk_init_environment_texture
    }

    DescriptorBufferInfo bbPosInfo{};
    bbPosInfo.buffer = engine->billboardPosSSBO;
    bbPosInfo.offset = 0;
    bbPosInfo.range = VK_WHOLE_SIZE;

    DescriptorBufferInfo bbMatInfo{};
    bbMatInfo.buffer = engine->billboardMatSSBO;
    bbMatInfo.offset = 0;
    bbMatInfo.range = VK_WHOLE_SIZE;

    DescriptorBufferInfo bbIndexInfo{};
    bbIndexInfo.buffer = engine->billboardBuffer;
    bbIndexInfo.offset = 0;
    bbIndexInfo.range = VK_WHOLE_SIZE;

    DescriptorBufferInfo transformBufferInfo{};
    transformBufferInfo.buffer = engine->transformBuffer;
    transformBufferInfo.offset = 0;
    transformBufferInfo.range = VK_WHOLE_SIZE;

    WriteDescriptorSet writes[10] = {};
    writes[0].dstSet = engine->descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = DescriptorType::UniformBuffer;
    writes[0].pBufferInfo = &bi;
    writes[0].pImageInfo = nullptr;

    writes[1].dstSet = engine->descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = DescriptorType::CombinedImageSampler;
    writes[1].pImageInfo = &envInfo;
    writes[1].pBufferInfo = nullptr;

    writes[2].dstSet = engine->descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].dstArrayElement = 0;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = DescriptorType::CombinedImageSampler;
    writes[2].pImageInfo = &irrInfo;
    writes[2].pBufferInfo = nullptr;

    writes[3].dstSet = engine->descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].dstArrayElement = 0;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = DescriptorType::CombinedImageSampler;
    writes[3].pImageInfo = &prefInfo;
    writes[3].pBufferInfo = nullptr;

    writes[4].dstSet = engine->descriptorSet;
    writes[4].dstBinding = 4;
    writes[4].dstArrayElement = 0;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType = DescriptorType::CombinedImageSampler;
    writes[4].pImageInfo = &lutInfo;
    writes[4].pBufferInfo = nullptr;

    writes[5].dstSet = engine->descriptorSet;
    writes[5].dstBinding = 5;
    writes[5].dstArrayElement = 0;
    writes[5].descriptorCount = 1;
    writes[5].descriptorType = DescriptorType::StorageBuffer;
    writes[5].pBufferInfo = &materialBufferInfo;
    writes[5].pImageInfo = nullptr;

    writes[6].dstSet = engine->descriptorSet;
    writes[6].dstBinding = 6;
    writes[6].dstArrayElement = 0;
    writes[6].descriptorCount = 1;
    writes[6].descriptorType = DescriptorType::StorageBuffer;
    writes[6].pBufferInfo = &bbPosInfo;
    writes[6].pImageInfo = nullptr;

    writes[7].dstSet = engine->descriptorSet;
    writes[7].dstBinding = 7;
    writes[7].dstArrayElement = 0;
    writes[7].descriptorCount = 1;
    writes[7].descriptorType = DescriptorType::StorageBuffer;
    writes[7].pBufferInfo = &bbMatInfo;
    writes[7].pImageInfo = nullptr;

    writes[8].dstSet = engine->descriptorSet;
    writes[8].dstBinding = 8;
    writes[8].dstArrayElement = 0;
    writes[8].descriptorCount = 1;
    writes[8].descriptorType = DescriptorType::StorageBuffer;
    writes[8].pBufferInfo = &bbIndexInfo;
    writes[8].pImageInfo = nullptr;

    writes[9].dstSet = engine->descriptorSet;
    writes[9].dstBinding = 9;
    writes[9].dstArrayElement = 0;
    writes[9].descriptorCount = 1;
    writes[9].descriptorType = DescriptorType::StorageBuffer;
    writes[9].pBufferInfo = &transformBufferInfo;
    writes[9].pImageInfo = nullptr;

    engine->appState->rhi->UpdateDescriptorSets(10, writes);
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
    glfwGetFramebufferSize(engine->appState->window, &width, &height);
    while (width == 0 || height == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(engine->appState->window, &width, &height);
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

    // Engine RHI is initialized in main.cpp, here we just initialize the internal engine parts
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

    engine->hdrIoThreadRunning = false;
    engine->hdrLoadInFlight = false;
    engine->pendingHdrIndex = -1;

    glfwSetInputMode(engine->appState->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Some values still depend on window layout
    glfwGetWindowPos(engine->appState->window, &engine->appState->core.window.windowedPosX, &engine->appState->core.window.windowedPosY);
    glfwGetWindowSize(engine->appState->window, &engine->appState->core.window.windowedWidth, &engine->appState->core.window.windowedHeight);
    if (!vk_start_hdr_io_thread(engine)) {
        vk_cleanup_vulkan_engine(engine);
        return false;
    }
    LOG_INFO("engine", "Vulkan initialise avec succes !");
    LOG_INFO("postprocess", "Default Exposure: %.2f", engine->appState->core.render.exposure);
    LOG_INFO("postprocess", "Default IBL Intensity: %.2f (Scale: %.2f)", engine->appState->core.render.iblIntensity,
             engine->appState->core.render.iblDebugScale);
    LOG_INFO("postprocess", "Default Tonemapper: Filmic ACES (DISABLED by default for Legacy OGL-ISO parity)");
    LOG_INFO("postprocess", "Default Color Grading: Sat=%.2f, Contrast=%.2f, Gamma=%.2f, Gain=%.2f, Offset=%.2f", engine->appState->core.render.saturation,
             engine->appState->core.render.contrast, engine->appState->core.render.gamma, engine->appState->core.render.gain,
             engine->appState->core.render.offset);
    LOG_INFO("postprocess", "Default White Balance: Temp=%.1f, Tint=%.2f", engine->appState->core.render.wbTemp, engine->appState->core.render.wbTint);
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
}
