#ifndef VK_ENGINE_H
#define VK_ENGINE_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdbool.h>
#include <vma/vk_mem_alloc.h>

// Configuration de GLM pour Vulkan
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define MAX_SWAPCHAIN_IMAGES 8

typedef struct {
    float position[3];
    float color[3];
} Vertex;

typedef struct {
    GLFWwindow* window;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    uint32_t graphicsQueueFamilyIndex;
    uint32_t presentQueueFamilyIndex;

    VmaAllocator allocator;

    VkSwapchainKHR swapchain;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;

    uint32_t imageCount;
    VkImage swapchainImages[MAX_SWAPCHAIN_IMAGES];
    VkImageView swapchainImageViews[MAX_SWAPCHAIN_IMAGES];
    VkFramebuffer swapchainFramebuffers[MAX_SWAPCHAIN_IMAGES];

    VkRenderPass renderPass;
    VkImage depthImage;
    VmaAllocation depthImageAllocation;
    VkImageView depthImageView;
    VkFormat depthFormat;

    // NOUVEAU : Le Layout de notre descripteur
    VkDescriptorSetLayout descriptorSetLayout;

    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;

    VkBuffer vertexBuffer;
    VmaAllocation vertexBufferAllocation;
    VkBuffer indexBuffer;
    VmaAllocation indexBufferAllocation;
    uint32_t indexCount;

    // NOUVEAU : Notre Uniform Buffer et son mapping persistant
    VkBuffer uniformBuffer;
    VmaAllocation uniformBufferAllocation;
    void* uniformBufferMapped;

    // NOUVEAU : Le pool et le set de descripteurs
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;

    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;

    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;
    uint32_t lastRenderedImageIndex;

} VulkanEngine;

bool init_vulkan_engine(VulkanEngine* engine);
bool draw_frame(VulkanEngine* engine);
void vk_set_object_name(VkDevice device, uint64_t handle, VkObjectType type, const char* name);
void vk_begin_label(VkDevice device, VkCommandBuffer cb, const char* name, float r, float g, float b);
void vk_end_label(VkDevice device, VkCommandBuffer cb);
void cleanup_vulkan_engine(VulkanEngine* engine);

#endif
