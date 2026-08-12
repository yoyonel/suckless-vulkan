

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

using namespace config;

namespace {

MaterialGpu make_default_material() {
    MaterialGpu material{};
    material.albedo_metallic[0] = 0.0f;
    material.albedo_metallic[1] = 0.0f;
    material.albedo_metallic[2] = 0.0f;
    material.albedo_metallic[3] = 0.0f;
    material.roughness_ao_pad[0] = kDefaultMaterialRoughness;
    material.roughness_ao_pad[1] = kDefaultMaterialAo;
    material.roughness_ao_pad[2] = 0.0f;
    material.roughness_ao_pad[3] = 0.0f;
    return material;
}

ResourceResult load_legacy_materials_for_grid(std::vector<MaterialGpu>& materials, size_t instanceCount) {
    if (MaterialLoader::load_materials(kMaterialJsonPath, materials) != ResourceResult::Success) {
        LOG_ERROR("material", "Failed to load material presets from %s", kMaterialJsonPath);
        return ResourceResult::ErrorParseFailed;
    }

    if (materials.size() != instanceCount) {
        if (materials.size() < instanceCount) {
            LOG_WARNING("material", "Material preset count (%zu) is lower than instance count (%zu); filling missing entries with legacy defaults.",
                        materials.size(), instanceCount);
        }
        materials.resize(instanceCount, make_default_material());
    }

    return ResourceResult::Success;
}

struct QueueFamilySelection {
    uint32_t graphicsFamily = UINT32_MAX;
    uint32_t presentFamily = UINT32_MAX;
    uint32_t transferFamily = UINT32_MAX;
    uint32_t computeFamily = UINT32_MAX;

    bool isComplete() const {
        return graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX;
    }
};

GfxResult has_required_device_extensions(VkPhysicalDevice physicalDevice) {
    uint32_t extensionCount = 0;
    if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr) != VK_SUCCESS) {
        return GfxResult::ErrorInitializationFailed;
    }

    std::vector<VkExtensionProperties> extensions(extensionCount);
    if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data()) != VK_SUCCESS) {
        return GfxResult::ErrorInitializationFailed;
    }

    for (const auto& extension : extensions) {
        if (strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            return GfxResult::Success;
        }
    }

    return GfxResult::ErrorUnsupportedFeature;
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

        // Dedicated Compute (has compute but NOT graphics)
        if ((queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0 && (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
            selection.computeFamily = index;
        }

        // Dedicated Transfer (has transfer but NOT graphics and NOT compute)
        if ((queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0 && (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 &&
            (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) == 0) {
            selection.transferFamily = index;
        }

        VkBool32 presentSupport = VK_FALSE;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, index, surface, &presentSupport) == VK_SUCCESS && presentSupport == VK_TRUE) {
            selection.presentFamily = index;
        }
    }

    // Fallbacks if dedicated queues aren't found
    if (selection.computeFamily == UINT32_MAX) {
        selection.computeFamily = selection.graphicsFamily;
    }
    if (selection.transferFamily == UINT32_MAX) {
        selection.transferFamily = selection.computeFamily;
    }

    return selection;
}

template <typename Handle, typename DestroyFn> void destroy_device_handle(VkDevice device, Handle& handle, DestroyFn destroyFn) {
    if (device == VK_NULL_HANDLE || handle == VK_NULL_HANDLE) {
        return;
    }

    destroyFn(device, handle, nullptr);
    handle = VK_NULL_HANDLE;
}

void cleanup_sync_objects(VulkanEngine* engine) {
    destroy_device_handle(engine->ctx.device, engine->renderFinishedSemaphore, vkDestroySemaphore);
    destroy_device_handle(engine->ctx.device, engine->imageAvailableSemaphore, vkDestroySemaphore);
    destroy_device_handle(engine->ctx.device, engine->inFlightFence, vkDestroyFence);
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
    engine->swapchainMgr.cleanup_dependent_resources(engine);
    destroy_device_handle(engine->ctx.device, engine->renderPass, vkDestroyRenderPass);
    destroy_device_handle(engine->ctx.device, engine->ctx.transferCompleteSemaphore, vkDestroySemaphore);
    destroy_device_handle(engine->ctx.device, engine->ctx.transferCommandPool, vkDestroyCommandPool);
    destroy_device_handle(engine->ctx.device, engine->ctx.commandPool, vkDestroyCommandPool);
}

void cleanup_raii_resources(VulkanEngine* engine) {
    engine->globalDescriptorPool.Reset();

    engine->swapchainMgr.depthImage.Reset();
    engine->envHdrImage.Reset();
    engine->envHdrSampler.Reset();

    engine->iblBaker.Cleanup();

    engine->graphicsPipeline.Reset();
    engine->billboardPipeline.Reset();
    engine->wireframePipeline.Reset();
    engine->debugLinePipeline.Reset();
    engine->debugTrianglePipeline.Reset();
    engine->skyboxPipeline.Reset();

    engine->pipelineLayout.Reset();
    engine->debugPipelineLayout.Reset();

    engine->globalDescriptorLayout.Reset();

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
    if (engine->ctx.allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(engine->ctx.allocator);
        engine->ctx.allocator = VK_NULL_HANDLE;
    }

    if (engine->ctx.device != VK_NULL_HANDLE) {
        vkDestroyDevice(engine->ctx.device, nullptr);
        engine->ctx.device = VK_NULL_HANDLE;
    }

    if (engine->ctx.instance != VK_NULL_HANDLE && engine->ctx.surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(engine->ctx.instance, engine->ctx.surface, nullptr);
        engine->ctx.surface = VK_NULL_HANDLE;
    }

    if (engine->ctx.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(engine->ctx.instance, nullptr);
        engine->ctx.instance = VK_NULL_HANDLE;
    }
}

bool is_uma_architecture(VkPhysicalDevice physicalDevice) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            return true;
        }
    }
    return false;
}

GfxResult select_physical_device(VulkanEngine* engine, const std::vector<VkPhysicalDevice>& devices) {
    QueueFamilySelection queueSelection;
    for (const auto& physicalDevice : devices) {
        if (has_required_device_extensions(physicalDevice) != GfxResult::Success) {
            continue;
        }

        queueSelection = find_queue_families(physicalDevice, engine->ctx.surface);
        if (!queueSelection.isComplete()) {
            continue;
        }

        if (device_supports_swapchain(physicalDevice, engine->ctx.surface) != GfxResult::Success) {
            continue;
        }

        engine->ctx.physicalDevice = physicalDevice;
        engine->ctx.graphicsQueueFamilyIndex = queueSelection.graphicsFamily;
        engine->ctx.presentQueueFamilyIndex = queueSelection.presentFamily;
        engine->ctx.transferQueueFamilyIndex = queueSelection.transferFamily;
        engine->ctx.computeQueueFamilyIndex = queueSelection.computeFamily;

        engine->ctx.isUMA = is_uma_architecture(physicalDevice);

        if (engine->ctx.transferQueueFamilyIndex != engine->ctx.graphicsQueueFamilyIndex &&
            engine->ctx.transferQueueFamilyIndex != engine->ctx.computeQueueFamilyIndex) {
            engine->ctx.hasDedicatedTransferQueue = true;
        }
        if (engine->ctx.computeQueueFamilyIndex != engine->ctx.graphicsQueueFamilyIndex) {
            engine->ctx.hasDedicatedComputeQueue = true;
        }
        return GfxResult::Success;
    }
    return GfxResult::ErrorUnsupportedFeature;
}

GfxResult init_core(VulkanEngine* engine) {
    if (engine->appState->window == nullptr) {
        return GfxResult::ErrorInitializationFailed;
    }

    glfwSetWindowUserPointer(engine->appState->window, engine);
    glfwSetCursorPosCallback(engine->appState->window, vk_mouse_callback);
    glfwSetScrollCallback(engine->appState->window, vk_scroll_callback);

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.apiVersion = VK_API_VERSION_1_4;

    uint32_t glfwExtCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtCount);
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (vkCreateInstance(&createInfo, NULL, &engine->ctx.instance) != VK_SUCCESS)
        return GfxResult::ErrorInitializationFailed;
    if (glfwCreateWindowSurface(engine->ctx.instance, engine->appState->window, NULL, &engine->ctx.surface) != VK_SUCCESS)
        return GfxResult::ErrorInitializationFailed;

    uint32_t deviceCount = 0;
    if (vkEnumeratePhysicalDevices(engine->ctx.instance, &deviceCount, NULL) != VK_SUCCESS || deviceCount == 0) {
        return GfxResult::ErrorInitializationFailed;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    if (vkEnumeratePhysicalDevices(engine->ctx.instance, &deviceCount, devices.data()) != VK_SUCCESS) {
        return GfxResult::ErrorInitializationFailed;
    }

    if (GfxResult res = select_physical_device(engine, devices); res != GfxResult::Success) {
        return res;
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfos[4] = {};
    uint32_t queueInfoCount = 0;

    std::vector<uint32_t> uniqueQueueFamilies = {engine->ctx.graphicsQueueFamilyIndex};

    auto add_unique = [&](uint32_t qf) {
        if (std::find(uniqueQueueFamilies.begin(), uniqueQueueFamilies.end(), qf) == uniqueQueueFamilies.end()) {
            uniqueQueueFamilies.push_back(qf);
        }
    };
    add_unique(engine->ctx.presentQueueFamilyIndex);
    add_unique(engine->ctx.transferQueueFamilyIndex);
    add_unique(engine->ctx.computeQueueFamilyIndex);

    for (uint32_t qf : uniqueQueueFamilies) {
        queueInfos[queueInfoCount].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfos[queueInfoCount].queueFamilyIndex = qf;
        queueInfos[queueInfoCount].queueCount = 1;
        queueInfos[queueInfoCount].pQueuePriorities = &queuePriority;
        queueInfoCount++;
    }

    const char* deviceExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkPhysicalDeviceVulkan14Features features14{};
    features14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
    features14.hostImageCopy = VK_TRUE;

    VkPhysicalDeviceFeatures2 deviceFeatures2{};
    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures2.features.samplerAnisotropy = VK_TRUE;
    deviceFeatures2.features.fillModeNonSolid = VK_TRUE;
    deviceFeatures2.pNext = &features14;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &deviceFeatures2;
    deviceInfo.queueCreateInfoCount = queueInfoCount;
    deviceInfo.pQueueCreateInfos = queueInfos;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExt;
    deviceInfo.pEnabledFeatures = nullptr;

    if (vkCreateDevice(engine->ctx.physicalDevice, &deviceInfo, NULL, &engine->ctx.device) != VK_SUCCESS)
        return GfxResult::ErrorInitializationFailed;
    vk_set_object_name(engine->ctx.device, (uint64_t)engine->ctx.device, VK_OBJECT_TYPE_DEVICE, "Logical_Device");

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(engine->ctx.physicalDevice, &props);
    const uint32_t v = props.driverVersion;
    LOG_INFO("suckless-vulkan.window", "Context Version: %u.%u", VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion));
    LOG_INFO("suckless-vulkan.window", "Renderer: %s", props.deviceName);
    LOG_INFO("suckless-vulkan.window", "Version: %u.%u.%u (Driver)", VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v));
    LOG_INFO("suckless-vulkan.window", "Architecture UMA: %s", engine->ctx.isUMA ? "Yes" : "No");
    LOG_INFO("suckless-vulkan.window", "Dedicated Transfer Queue: %s (Family %u)", engine->ctx.hasDedicatedTransferQueue ? "Yes" : "No",
             engine->ctx.transferQueueFamilyIndex);
    LOG_INFO("suckless-vulkan.window", "Dedicated Compute Queue: %s (Family %u)", engine->ctx.hasDedicatedComputeQueue ? "Yes" : "No",
             engine->ctx.computeQueueFamilyIndex);

    vkGetDeviceQueue(engine->ctx.device, engine->ctx.graphicsQueueFamilyIndex, 0, &engine->ctx.graphicsQueue);
    vkGetDeviceQueue(engine->ctx.device, engine->ctx.presentQueueFamilyIndex, 0, &engine->ctx.presentQueue);
    vkGetDeviceQueue(engine->ctx.device, engine->ctx.transferQueueFamilyIndex, 0, &engine->ctx.transferQueue);
    vkGetDeviceQueue(engine->ctx.device, engine->ctx.computeQueueFamilyIndex, 0, &engine->ctx.computeQueue);

    vk_set_object_name(engine->ctx.device, (uint64_t)engine->ctx.graphicsQueue, VK_OBJECT_TYPE_QUEUE, "Graphics_Queue");
    vk_set_object_name(engine->ctx.device, (uint64_t)engine->ctx.presentQueue, VK_OBJECT_TYPE_QUEUE, "Present_Queue");
    if (engine->ctx.hasDedicatedTransferQueue)
        vk_set_object_name(engine->ctx.device, (uint64_t)engine->ctx.transferQueue, VK_OBJECT_TYPE_QUEUE, "Transfer_Queue");
    if (engine->ctx.hasDedicatedComputeQueue)
        vk_set_object_name(engine->ctx.device, (uint64_t)engine->ctx.computeQueue, VK_OBJECT_TYPE_QUEUE, "Compute_Queue");

    return GfxResult::Success;
}

GfxResult init_allocator(VulkanEngine* engine) {
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = engine->ctx.physicalDevice;
    allocatorInfo.device = engine->ctx.device;
    allocatorInfo.instance = engine->ctx.instance;
    return (vmaCreateAllocator(&allocatorInfo, &engine->ctx.allocator) == VK_SUCCESS) ? GfxResult::Success : GfxResult::ErrorInitializationFailed;
}

GfxResult init_render_pass(VulkanEngine* engine) {
    VkAttachmentDescription attachments[2] = {};
    attachments[0].format = engine->swapchainMgr.swapchainImageFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format = engine->swapchainMgr.depthFormat;
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

    if (vkCreateRenderPass(engine->ctx.device, &rpInfo, NULL, &engine->renderPass) != VK_SUCCESS)
        return GfxResult::ErrorInitializationFailed;
    vk_set_object_name(engine->ctx.device, (uint64_t)engine->renderPass, VK_OBJECT_TYPE_RENDER_PASS, "Main_RenderPass");

    for (uint32_t i = 0; i < engine->swapchainMgr.imageCount; i++) {
        VkImageView depthImageView = ((VulkanRHI*)engine->appState->rhi)->GetVkImageView(engine->swapchainMgr.depthImage);
        VkImageView fbAtt[] = {engine->swapchainMgr.swapchainImageViews[i], depthImageView};
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = engine->renderPass;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments = fbAtt;
        fbInfo.width = engine->swapchainMgr.swapchainExtent.width;
        fbInfo.height = engine->swapchainMgr.swapchainExtent.height;
        fbInfo.layers = 1;
        if (vkCreateFramebuffer(engine->ctx.device, &fbInfo, NULL, &engine->swapchainMgr.swapchainFramebuffers[i]) != VK_SUCCESS) {
            return GfxResult::ErrorInitializationFailed;
        }
        const std::string framebufferName = "Swapchain_Framebuffer_" + std::to_string(i);
        vk_set_object_name(engine->ctx.device, (uint64_t)engine->swapchainMgr.swapchainFramebuffers[i], VK_OBJECT_TYPE_FRAMEBUFFER, framebufferName.c_str());
    }
    return GfxResult::Success;
}

GfxResult init_descriptor_layout(VulkanEngine* engine) {
    std::vector<DescriptorSetLayoutBinding> bindings = {
        {0, DescriptorType::UniformBuffer, 1, ShaderStage::AllGraphics},     {1, DescriptorType::CombinedImageSampler, 1, ShaderStage::Fragment},
        {2, DescriptorType::CombinedImageSampler, 1, ShaderStage::Fragment}, {3, DescriptorType::CombinedImageSampler, 1, ShaderStage::Fragment},
        {4, DescriptorType::CombinedImageSampler, 1, ShaderStage::Fragment}, {5, DescriptorType::StorageBuffer, 1, ShaderStage::Fragment},
        {6, DescriptorType::StorageBuffer, 1, ShaderStage::Vertex},          {7, DescriptorType::StorageBuffer, 1, ShaderStage::Vertex},
        {8, DescriptorType::StorageBuffer, 1, ShaderStage::Vertex},          {9, DescriptorType::StorageBuffer, 1, ShaderStage::Vertex}};
    DescriptorLayoutDesc desc{bindings.data(), static_cast<uint32_t>(bindings.size())};
    engine->globalDescriptorLayout.Reset(engine->appState->rhi, engine->appState->rhi->CreateDescriptorLayout(desc, "Global_DescriptorSetLayout"));
    return engine->globalDescriptorLayout.is_valid() ? GfxResult::Success : GfxResult::ErrorInitializationFailed;
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

GfxResult create_main_graphics_pipeline(VulkanEngine* engine, const std::vector<uint32_t>& vsm, const std::vector<uint32_t>& fsm) {
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
    return engine->graphicsPipeline.is_valid() ? GfxResult::Success : GfxResult::ErrorInitializationFailed;
}

GfxResult create_skybox_pipeline(VulkanEngine* engine, const std::vector<uint32_t>& vsm, const std::vector<uint32_t>& fsm) {
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
    return engine->skyboxPipeline.is_valid() ? GfxResult::Success : GfxResult::ErrorInitializationFailed;
}

GfxResult create_billboard_pipeline(VulkanEngine* engine) {
    auto bvm = load_shader("shaders/billboard_vert.spv");
    auto bfm = load_shader("shaders/billboard_frag.spv");
    if (bvm.empty() || bfm.empty())
        return GfxResult::ErrorInitializationFailed;

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
    return engine->billboardPipeline.is_valid() ? GfxResult::Success : GfxResult::ErrorInitializationFailed;
}

GfxResult create_wireframe_pipeline(VulkanEngine* engine, const std::vector<uint32_t>& vsm, const std::vector<uint32_t>& fsm) {
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
    return engine->wireframePipeline.is_valid() ? GfxResult::Success : GfxResult::ErrorInitializationFailed;
}

GfxResult create_debug_pipelines(VulkanEngine* engine) {
    auto dvm = load_shader("shaders/debug_vert.spv");
    auto dfm = load_shader("shaders/debug_frag.spv");
    if (dvm.empty() || dfm.empty())
        return GfxResult::ErrorInitializationFailed;

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
    desc.cullMode = CullMode::Back;
    desc.colorBlendEnable = true;
    desc.debugName = "Debug_Triangle_Pipeline";
    engine->debugTrianglePipeline.Reset(engine->appState->rhi, engine->appState->rhi->CreateGraphicsPipeline(desc));
    return engine->debugLinePipeline.is_valid() && engine->debugTrianglePipeline.is_valid() ? GfxResult::Success : GfxResult::ErrorInitializationFailed;
}

GfxResult init_pipeline(VulkanEngine* engine) {
    DescriptorLayoutHandle d[] = {engine->globalDescriptorLayout};
    PipelineLayoutDesc plDesc{d, 1, nullptr, 0};
    engine->pipelineLayout.Reset(engine->appState->rhi, engine->appState->rhi->CreatePipelineLayout(plDesc, "Main_Pipeline_Layout"));

    auto vsm = load_shader("shaders/vert.spv");
    auto fsm = load_shader("shaders/frag.spv");
    auto skyboxVsm = load_shader("shaders/skybox_vert.spv");
    auto skyboxFsm = load_shader("shaders/skybox_frag.spv");
    if (vsm.empty() || fsm.empty() || skyboxVsm.empty() || skyboxFsm.empty())
        return GfxResult::ErrorInitializationFailed;

    GfxResult success = GfxResult::Success;
    if (create_main_graphics_pipeline(engine, vsm, fsm) != GfxResult::Success)
        success = GfxResult::ErrorInitializationFailed;
    if (create_skybox_pipeline(engine, skyboxVsm, skyboxFsm) != GfxResult::Success)
        success = GfxResult::ErrorInitializationFailed;
    if (create_wireframe_pipeline(engine, vsm, fsm) != GfxResult::Success)
        success = GfxResult::ErrorInitializationFailed;
    if (create_billboard_pipeline(engine) != GfxResult::Success)
        success = GfxResult::ErrorInitializationFailed;
    if (create_debug_pipelines(engine) != GfxResult::Success)
        success = GfxResult::ErrorInitializationFailed;

    return success;
}

GfxResult create_icosphere_buffers(VulkanEngine* engine, const Icosphere& sphere) {
    engine->vertexBuffer.Reset(engine->appState->rhi, engine->appState->rhi->CreateBuffer(sphere.vertices.size() * sizeof(Vertex), BufferUsage::Vertex,
                                                                                          sphere.vertices.data(), "Icosphere_Vertex_Buffer"));
    if (!engine->vertexBuffer.is_valid())
        return GfxResult::ErrorInitializationFailed;

    engine->indexBuffer.Reset(engine->appState->rhi, engine->appState->rhi->CreateBuffer(sphere.indices.size() * sizeof(uint32_t), BufferUsage::Index,
                                                                                         sphere.indices.data(), "Icosphere_Index_Buffer"));
    return engine->indexBuffer.is_valid() ? GfxResult::Success : GfxResult::ErrorInitializationFailed;
}

GfxResult create_instance_grid_buffers(VulkanEngine* engine, std::vector<glm::vec3>& instancePositions) {
    const size_t instanceCount = kMaterialInstanceCount;
    engine->appState->core.scene.instanceCount = static_cast<uint32_t>(instanceCount);
    engine->appState->core.scene.instancePositions =
        static_cast<glm::vec3*>(arena_alloc(&engine->appState->core.scene.arena, instanceCount * sizeof(glm::vec3), 64));
    instancePositions.resize(instanceCount);

    for (uint32_t row = 0; row < kGridSize; ++row) {
        for (uint32_t col = 0; col < kGridSize; ++col) {
            const size_t instanceIndex = (static_cast<size_t>(row) * static_cast<size_t>(kGridSize)) + static_cast<size_t>(col);
            const float x = (static_cast<float>(col) * kGridSpacingMeters) - kGridOffset;
            const float y = -((static_cast<float>(row) * kGridSpacingMeters) - kGridOffset);
            engine->appState->core.scene.instancePositions[instanceIndex] = {x, y, 0.0f};
            instancePositions[instanceIndex] = {x, y, 0.0f};
        }
    }
    engine->transformBuffer.Reset(engine->appState->rhi,
                                  engine->appState->rhi->CreateBuffer(instanceCount * sizeof(glm::vec4), BufferUsage::Storage, nullptr, "Flat_Transform_SSBO"));
    if (engine->transformBuffer.is_valid()) {
        engine->transformBufferMapped = engine->appState->rhi->MapBuffer(engine->transformBuffer);
    }
    return engine->transformBuffer.is_valid() ? GfxResult::Success : GfxResult::ErrorInitializationFailed;
}

GfxResult create_billboard_instance_buffer(VulkanEngine* engine, const std::vector<glm::vec3>& instancePositions) {
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

    return engine->billboardBuffer.is_valid() && engine->billboardPosSSBO.is_valid() && engine->billboardMatSSBO.is_valid()
               ? GfxResult::Success
               : GfxResult::ErrorInitializationFailed;
}

GfxResult create_material_ssbo(VulkanEngine* engine) {
    std::vector<MaterialGpu> materials;
    if (load_legacy_materials_for_grid(materials, kMaterialInstanceCount) != ResourceResult::Success) {
        return GfxResult::ErrorInitializationFailed;
    }
    if (materials.empty()) {
        return GfxResult::Success;
    }
    engine->materialBuffer.Reset(engine->appState->rhi, engine->appState->rhi->CreateBuffer(materials.size() * sizeof(MaterialGpu), BufferUsage::Storage,
                                                                                            materials.data(), "PBR_Materials_SSBO"));
    return engine->materialBuffer.is_valid() ? GfxResult::Success : GfxResult::ErrorInitializationFailed;
}

GfxResult create_global_uniform_buffer(VulkanEngine* engine) {
    engine->uniformBuffer.Reset(engine->appState->rhi, engine->appState->rhi->CreateBuffer(sizeof(UBOData), BufferUsage::Uniform, nullptr, "Global_MVP_UBO"));
    if (!engine->uniformBuffer.is_valid())
        return GfxResult::ErrorInitializationFailed;
    engine->uniformBufferMapped = engine->appState->rhi->MapBuffer(engine->uniformBuffer);
    return engine->uniformBufferMapped != nullptr ? GfxResult::Success : GfxResult::ErrorInitializationFailed;
}

GfxResult init_buffers(VulkanEngine* engine) {
    Icosphere sphere;
    sphere.generate(3);
    engine->indexCount = static_cast<uint32_t>(sphere.indices.size());

    VkCommandPoolCreateInfo cpIn{};
    cpIn.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpIn.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpIn.queueFamilyIndex = engine->ctx.graphicsQueueFamilyIndex;
    if (vkCreateCommandPool(engine->ctx.device, &cpIn, nullptr, &engine->ctx.commandPool) != VK_SUCCESS) {
        return GfxResult::ErrorInitializationFailed;
    }

    if (engine->ctx.hasDedicatedTransferQueue) {
        VkCommandPoolCreateInfo tcpIn{};
        tcpIn.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        tcpIn.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        tcpIn.queueFamilyIndex = engine->ctx.transferQueueFamilyIndex;
        if (vkCreateCommandPool(engine->ctx.device, &tcpIn, nullptr, &engine->ctx.transferCommandPool) != VK_SUCCESS) {
            return GfxResult::ErrorInitializationFailed;
        }
        vk_set_object_name(engine->ctx.device, (uint64_t)engine->ctx.transferCommandPool, VK_OBJECT_TYPE_COMMAND_POOL, "Transfer_CommandPool");

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateSemaphore(engine->ctx.device, &semInfo, nullptr, &engine->ctx.transferCompleteSemaphore) != VK_SUCCESS) {
            return GfxResult::ErrorInitializationFailed;
        }
        vk_set_object_name(engine->ctx.device, (uint64_t)engine->ctx.transferCompleteSemaphore, VK_OBJECT_TYPE_SEMAPHORE, "Transfer_CompleteSemaphore");
    }
    vk_set_object_name(engine->ctx.device, (uint64_t)engine->ctx.commandPool, VK_OBJECT_TYPE_COMMAND_POOL, "Main_Command_Pool");

    std::vector<glm::vec3> instancePositions;
    GfxResult success = GfxResult::Success;
    if (create_icosphere_buffers(engine, sphere) != GfxResult::Success)
        success = GfxResult::ErrorInitializationFailed;
    if (create_instance_grid_buffers(engine, instancePositions) != GfxResult::Success)
        success = GfxResult::ErrorInitializationFailed;
    if (create_billboard_instance_buffer(engine, instancePositions) != GfxResult::Success)
        success = GfxResult::ErrorInitializationFailed;
    if (create_material_ssbo(engine) != GfxResult::Success)
        success = GfxResult::ErrorInitializationFailed;
    if (create_global_uniform_buffer(engine) != GfxResult::Success)
        success = GfxResult::ErrorInitializationFailed;

    return success;
}

GfxResult init_descriptor_pool_and_sets(VulkanEngine* engine) {
    DescriptorPoolSize sizes[3] = {{DescriptorType::UniformBuffer, 1}, {DescriptorType::CombinedImageSampler, 4}, {DescriptorType::StorageBuffer, 5}};
    DescriptorPoolDesc desc{sizes, 3, 1};
    engine->globalDescriptorPool.Reset(engine->appState->rhi, engine->appState->rhi->CreateDescriptorPool(desc, "Global_Descriptor_Pool"));
    if (!engine->globalDescriptorPool.is_valid()) {
        return GfxResult::ErrorInitializationFailed;
    }

    DescriptorSetAllocateDesc ai{};
    ai.pool = engine->globalDescriptorPool;
    ai.setCount = 1;
    DescriptorLayoutHandle layoutHandle = engine->globalDescriptorLayout;
    ai.layouts = &layoutHandle;
    if (engine->appState->rhi->AllocateDescriptorSets(ai, &engine->descriptorSet) != RHIResult::Success) {
        return GfxResult::ErrorInitializationFailed;
    }
    VkDescriptorSet vkSet = ((VulkanRHI*)engine->appState->rhi)->GetVkDescriptorSet(engine->descriptorSet);
    vk_set_object_name(engine->ctx.device, (uint64_t)vkSet, VK_OBJECT_TYPE_DESCRIPTOR_SET, "Global_Descriptor_Set");

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
        tex = engine->iblBaker.irradianceMap.is_valid() ? engine->iblBaker.irradianceMap : INVALID_HANDLE;
    envInfo.texture = tex;
    envInfo.imageView = INVALID_HANDLE;
    SamplerHandle samp = engine->envHdrSampler.is_valid() ? engine->envHdrSampler : INVALID_HANDLE;
    if (samp == INVALID_HANDLE)
        samp = engine->iblBaker.irradianceSampler.is_valid() ? engine->iblBaker.irradianceSampler : INVALID_HANDLE;
    envInfo.sampler = samp;
    envInfo.imageView = INVALID_HANDLE;
    envInfo.imageLayout = TextureLayout::ShaderReadOnlyOptimal;

    DescriptorImageInfo irrInfo{};
    irrInfo.texture = engine->iblBaker.irradianceMap.is_valid() ? engine->iblBaker.irradianceMap : engine->envHdrImage;
    irrInfo.sampler = engine->iblBaker.irradianceSampler.is_valid() ? engine->iblBaker.irradianceSampler : engine->envHdrSampler;
    irrInfo.imageView = INVALID_HANDLE;
    irrInfo.imageLayout = TextureLayout::ShaderReadOnlyOptimal;

    DescriptorImageInfo prefInfo{};
    prefInfo.texture = engine->iblBaker.prefilteredMap.is_valid() ? engine->iblBaker.prefilteredMap : engine->envHdrImage;
    prefInfo.sampler = engine->iblBaker.prefilteredSampler.is_valid() ? engine->iblBaker.prefilteredSampler : engine->envHdrSampler;
    prefInfo.imageView = INVALID_HANDLE;
    prefInfo.imageLayout = TextureLayout::ShaderReadOnlyOptimal;

    DescriptorImageInfo lutInfo{};
    lutInfo.texture = engine->iblBaker.brdfLut.is_valid() ? engine->iblBaker.brdfLut : engine->envHdrImage;
    lutInfo.sampler = engine->iblBaker.brdfLutSampler.is_valid() ? engine->iblBaker.brdfLutSampler : engine->envHdrSampler;
    lutInfo.imageView = INVALID_HANDLE;
    lutInfo.imageLayout = TextureLayout::ShaderReadOnlyOptimal;

    // Safety: ensure no invalid handles are passed for required bindings
    if (envInfo.texture == INVALID_HANDLE || irrInfo.texture == INVALID_HANDLE || prefInfo.texture == INVALID_HANDLE || lutInfo.texture == INVALID_HANDLE) {
        LOG_ERROR("engine", "init_descriptor_pool_and_sets: one or more required images are INVALID (spec violation). Skipping initial update.");
        return GfxResult::Success; // We'll update later in vk_init_environment_texture
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
    return GfxResult::Success;
}

GfxResult init_commands_and_sync(VulkanEngine* engine) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = engine->ctx.commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(engine->ctx.device, &ai, &engine->commandBuffer) != VK_SUCCESS) {
        return GfxResult::ErrorInitializationFailed;
    }
    vk_set_object_name(engine->ctx.device, (uint64_t)engine->commandBuffer, VK_OBJECT_TYPE_COMMAND_BUFFER, "Main_CommandBuffer");

    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateSemaphore(engine->ctx.device, &si, nullptr, &engine->imageAvailableSemaphore) != VK_SUCCESS) {
        return GfxResult::ErrorInitializationFailed;
    }
    vk_set_object_name(engine->ctx.device, (uint64_t)engine->imageAvailableSemaphore, VK_OBJECT_TYPE_SEMAPHORE, "Image_Available_Semaphore");
    if (vkCreateSemaphore(engine->ctx.device, &si, nullptr, &engine->renderFinishedSemaphore) != VK_SUCCESS) {
        return GfxResult::ErrorInitializationFailed;
    }
    vk_set_object_name(engine->ctx.device, (uint64_t)engine->renderFinishedSemaphore, VK_OBJECT_TYPE_SEMAPHORE, "Render_Finished_Semaphore");
    if (vkCreateFence(engine->ctx.device, &fi, nullptr, &engine->inFlightFence) != VK_SUCCESS) {
        return GfxResult::ErrorInitializationFailed;
    }
    vk_set_object_name(engine->ctx.device, (uint64_t)engine->inFlightFence, VK_OBJECT_TYPE_FENCE, "Main_Render_Fence");
    return GfxResult::Success;
}

} // namespace

// --- FONCTIONS PUBLIQUES (vk_engine_init) ---

GfxResult vk_recreate_swapchain(VulkanEngine* engine) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(engine->appState->window, &width, &height);
    while (width == 0 || height == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(engine->appState->window, &width, &height);
    }

    if (vkDeviceWaitIdle(engine->ctx.device) != VK_SUCCESS) {
        return GfxResult::ErrorInitializationFailed;
    }

    engine->swapchainMgr.cleanup_dependent_resources(engine);
    destroy_device_handle(engine->ctx.device, engine->renderPass, vkDestroyRenderPass);

    return (engine->swapchainMgr.init(engine) == GfxResult::Success && init_render_pass(engine) == GfxResult::Success &&
            init_pipeline(engine) == GfxResult::Success)
               ? GfxResult::Success
               : GfxResult::ErrorInitializationFailed;
}

GfxResult vk_init_vulkan_engine(VulkanEngine* engine) {
    SVK_TRACY_ZONE_SCOPED("vk_init_vulkan_engine");
    LOG_INFO("app", "Starting engine initialization...");
    LOG_INFO("vulkan", "Vulkan Debug Callback initialized (High Sensitivity)");

    LOG_INFO("app", "init_core...");
    if (init_core(engine) != GfxResult::Success) {
        LOG_ERROR("app", "init_core failed");
        return GfxResult::ErrorInitializationFailed;
    }
    LOG_INFO("app", "init_allocator...");
    if (init_allocator(engine) != GfxResult::Success) {
        LOG_ERROR("app", "init_allocator failed");
        return GfxResult::ErrorInitializationFailed;
    }

    // Engine RHI is initialized in main.cpp, here we just initialize the internal engine parts
    LOG_INFO("app", "init_swapchain...");
    if (engine->swapchainMgr.init(engine) != GfxResult::Success) {
        LOG_ERROR("app", "init_swapchain failed");
        return GfxResult::ErrorInitializationFailed;
    }
    LOG_INFO("app", "init_render_pass...");
    if (init_render_pass(engine) != GfxResult::Success) {
        LOG_ERROR("app", "init_render_pass failed");
        return GfxResult::ErrorInitializationFailed;
    }
    LOG_INFO("app", "init_descriptor_layout...");
    if (init_descriptor_layout(engine) != GfxResult::Success) {
        LOG_ERROR("app", "init_descriptor_layout failed");
        return GfxResult::ErrorInitializationFailed;
    }
    LOG_INFO("app", "init_pipeline...");
    if (init_pipeline(engine) != GfxResult::Success) {
        LOG_ERROR("app", "init_pipeline failed");
        return GfxResult::ErrorInitializationFailed;
    }
    LOG_INFO("app", "init_buffers...");
    if (init_buffers(engine) != GfxResult::Success) {
        LOG_ERROR("app", "init_buffers failed");
        return GfxResult::ErrorInitializationFailed;
    }
    LOG_INFO("app", "vk_init_environment_catalog...");
    if (vk_init_environment_catalog(engine) != GfxResult::Success) {
        LOG_ERROR("app", "vk_init_environment_catalog failed");
        return GfxResult::ErrorInitializationFailed;
    }
    if (init_ibl(engine) != GfxResult::Success) {
        LOG_ERROR("app", "init_ibl failed");
        return GfxResult::ErrorInitializationFailed;
    }

    LOG_INFO("app", "Initializing commands and sync objects...");
    if (init_commands_and_sync(engine) != GfxResult::Success) {
        LOG_ERROR("app", "init_commands_and_sync failed");
        return GfxResult::ErrorInitializationFailed;
    }

    if (tracy_vk_context_init(engine) != GfxResult::Success) {
        LOG_ERROR("tracy", "tracy_vk_context_init failed");
        return GfxResult::ErrorInitializationFailed;
    }

    LOG_INFO("app", "Initializing environment texture (triggers bake)...");
    if (vk_init_environment_texture(engine) != GfxResult::Success) {
        LOG_ERROR("app", "vk_init_environment_texture failed");
        return GfxResult::ErrorInitializationFailed;
    }

    LOG_INFO("app", "Initializing descriptor pool and sets (commandPool=%p)...", (void*)engine->ctx.commandPool);
    if (init_descriptor_pool_and_sets(engine) != GfxResult::Success) {
        LOG_ERROR("app", "init_descriptor_pool_and_sets failed");
        return GfxResult::ErrorInitializationFailed;
    }

    LOG_INFO("app", "Initialization complete.");

    engine->io.hdrIoThreadRunning = false;
    engine->io.hdrLoadInFlight = false;
    engine->io.pendingHdrIndex = -1;

    glfwSetInputMode(engine->appState->window, GLFW_CURSOR, engine->appState->core.cameraEnabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

    // Some values still depend on window layout
    glfwGetWindowPos(engine->appState->window, &engine->appState->core.window.windowedPosX, &engine->appState->core.window.windowedPosY);
    glfwGetWindowSize(engine->appState->window, &engine->appState->core.window.windowedWidth, &engine->appState->core.window.windowedHeight);
    if (vk_start_hdr_io_thread(engine) != GfxResult::Success) {
        vk_cleanup_vulkan_engine(engine);
        return GfxResult::ErrorInitializationFailed;
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
    return GfxResult::Success;
}

void vk_cleanup_vulkan_engine(VulkanEngine* engine) {
    SVK_TRACY_ZONE_SCOPED("vk_cleanup_vulkan_engine");
    LOG_INFO("app", "Total frames rendered during this run: %llu", (unsigned long long)engine->totalFramesRendered);
    vk_stop_hdr_io_thread(engine);
    LOG_INFO("async", "Async loader destroyed");

    if (engine->ctx.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(engine->ctx.device);
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
