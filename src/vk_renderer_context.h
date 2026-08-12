#ifndef VK_RENDERER_CONTEXT_H
#define VK_RENDERER_CONTEXT_H

#include <vma/vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct RendererContext {
    VkInstance instance{VK_NULL_HANDLE};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};

    VkQueue graphicsQueue{VK_NULL_HANDLE};
    VkQueue presentQueue{VK_NULL_HANDLE};
    uint32_t graphicsQueueFamilyIndex{UINT32_MAX};
    uint32_t presentQueueFamilyIndex{UINT32_MAX};

    bool isUMA{false};
    bool hasDedicatedTransferQueue{false};
    bool hasDedicatedComputeQueue{false};

    uint32_t transferQueueFamilyIndex{UINT32_MAX};
    uint32_t computeQueueFamilyIndex{UINT32_MAX};
    VkQueue transferQueue{VK_NULL_HANDLE};
    VkQueue computeQueue{VK_NULL_HANDLE};

    VkCommandPool transferCommandPool{VK_NULL_HANDLE};
    VkSemaphore transferCompleteSemaphore{VK_NULL_HANDLE};

    VmaAllocator allocator{VK_NULL_HANDLE};
    VkCommandPool commandPool{VK_NULL_HANDLE};
};

#endif
