#ifndef VK_ENGINE_H
#define VK_ENGINE_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <stdbool.h>
#include <string>
#include <thread>
#include <vector>
#include <vma/vk_mem_alloc.h>

#include "camera.h"
#include "core_engine.h"
#include "rhi/rhi.h"

// Configuration de GLM pour Vulkan
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define MAX_SWAPCHAIN_IMAGES 8

// Phase 2B: Async HDR Loading
enum class HdrLoadRequestState : std::uint8_t {
    Pending, // Queued, waiting for I/O thread
    Loading, // I/O thread is reading file
    Ready,   // File loaded into memory, ready for GPU upload
    Failed   // Load failed (file not found, corrupt, etc.)
};

struct HdrLoadRequest {
    int hdrIndex;                  // Index in hdrFiles array
    HdrLoadRequestState state;     // Current load state
    std::vector<float> pixelData;  // CPU-side RGBA32F pixel data
    uint32_t width;                // Image width
    uint32_t height;               // Image height
    uint32_t channels;             // Channels (typically 4)
    std::string sourcePathOrLabel; // Source file path or fallback label
};

// Phase IBL-0: Synchronous Bake Resources
struct IblResources {
    TextureHandle irradianceMap{INVALID_HANDLE};
    SamplerHandle irradianceSampler{INVALID_HANDLE};

    TextureHandle prefilteredMap{INVALID_HANDLE};
    SamplerHandle prefilteredSampler{INVALID_HANDLE};

    TextureHandle brdfLut{INVALID_HANDLE};
    SamplerHandle brdfLutSampler{INVALID_HANDLE};

    // Internal Compute Resources (Luminance Reduction)
    VkBuffer lumGroupSumsBuffer;
    VmaAllocation lumGroupSumsAllocation;
    VkBuffer lumMeanBuffer;
    VmaAllocation lumMeanAllocation;

    // Compute Pipelines
    PipelineHandle irmapPipeline{INVALID_HANDLE};
    PipelineHandle spmapPipeline{INVALID_HANDLE};
    PipelineHandle brdfLutPipeline{INVALID_HANDLE};
    PipelineHandle lum1Pipeline{INVALID_HANDLE};
    PipelineHandle lum2Pipeline{INVALID_HANDLE};

    PipelineLayoutHandle iblPipelineLayout{INVALID_HANDLE};
    PipelineLayoutHandle lum1PipelineLayout{INVALID_HANDLE};
    PipelineLayoutHandle lum2PipelineLayout{INVALID_HANDLE};
    DescriptorLayoutHandle iblDescriptorSetLayout{INVALID_HANDLE};
    DescriptorLayoutHandle lum1DescriptorSetLayout{INVALID_HANDLE};
    DescriptorLayoutHandle lum2DescriptorSetLayout{INVALID_HANDLE};
    DescriptorPoolHandle computeDescriptorPool{INVALID_HANDLE};

    // Descriptor sets for individual compute passes
    DescriptorSetHandle lum1DescriptorSet{INVALID_HANDLE};
    DescriptorSetHandle lum2DescriptorSet{INVALID_HANDLE};
    DescriptorSetHandle irmapDescriptorSet{INVALID_HANDLE};
    DescriptorSetHandle spmapDescriptorSet{INVALID_HANDLE};
    DescriptorSetHandle brdfLutDescriptorSet{INVALID_HANDLE};

    bool brdfLutBaked;
    float bakedMeanLuminance;
};

struct Vertex {
    float position[3];
    float color[3];
};

struct UBOData {
    glm::mat4 vp;
    glm::mat4 modelRotation;
    glm::mat4 invViewProj;
    glm::vec4 cameraPosEnvLod;
    glm::vec4 debugParams; // x: mode, y: scale, z: billboardMode
    glm::vec4 postParams1; // x: exposure, y: saturation, z: contrast, w: gamma
    glm::vec4 postParams2; // x: gain, y: offset, z: wbTemp, w: wbTint
    glm::mat4 view;        // New for billboards
    glm::mat4 proj;        // New for billboards
    glm::vec4 windowSize;
};

struct DebugPushConstant {
    glm::mat4 model;
    glm::vec4 color;
    float radius;
    int mode;
    int stippled;
};

struct VulkanEngine {
    bool useNullRHI{false};
    struct GLFWwindow* window;
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
    TextureHandle depthImage;
    VkFormat depthFormat;

    // NOUVEAU : Le Layout de notre descripteur
    DescriptorLayoutHandle globalDescriptorLayout{INVALID_HANDLE};

    PipelineLayoutHandle pipelineLayout{INVALID_HANDLE};
    PipelineLayoutHandle debugPipelineLayout{INVALID_HANDLE};
    PipelineHandle graphicsPipeline{INVALID_HANDLE};
    PipelineHandle billboardPipeline{INVALID_HANDLE};
    PipelineHandle wireframePipeline{INVALID_HANDLE};
    PipelineHandle debugLinePipeline{INVALID_HANDLE};
    PipelineHandle debugTrianglePipeline{INVALID_HANDLE};
    PipelineHandle skyboxPipeline{INVALID_HANDLE};

    BufferHandle vertexBuffer{INVALID_HANDLE};
    BufferHandle indexBuffer{INVALID_HANDLE};
    uint32_t indexCount;

    // Instancing : buffer contenant les positions des 100 sphères
    BufferHandle instanceBuffer{INVALID_HANDLE};

    // Buffer (SSBO) pour stocker les 100 matériaux PBR
    BufferHandle materialBuffer;

    // Notre Uniform Buffer et son mapping persistant
    BufferHandle uniformBuffer;
    void* uniformBufferMapped;

    // NOUVEAU : Le pool et le set de descripteurs
    DescriptorPoolHandle globalDescriptorPool{INVALID_HANDLE};
    DescriptorSetHandle descriptorSet{INVALID_HANDLE};

    TextureHandle envHdrImage;
    SamplerHandle envHdrSampler;
    uint32_t envHdrMipLevels;
    uint32_t envHdrWidth;
    uint32_t envHdrHeight;
    std::vector<std::string> hdrFiles;
    int currentHdrIndex;

    // Phase 2B: Async HDR loading infrastructure
    std::queue<HdrLoadRequest> hdrLoadQueue;  // Requests queued for I/O thread
    std::queue<HdrLoadRequest> hdrReadyQueue; // Ready/failed requests for render thread
    std::thread hdrIoThread;                  // I/O worker thread
    std::mutex hdrLoadMutex;                  // Protect queue state
    std::condition_variable hdrLoadCV;        // Signal I/O thread on new requests
    bool hdrIoThreadRunning;                  // Control flag for I/O thread
    bool hdrLoadInFlight;                     // True while worker decodes one request
    int pendingHdrIndex;                      // Last requested HDR index (-1 if none)

    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;

    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;
    uint32_t lastRenderedImageIndex;
    void* tracyVkContext;

    BufferHandle billboardBuffer{INVALID_HANDLE};
    void* billboardMapped;

    IblResources ibl;

    CoreInput currentInput;
    CoreEngine core;
    IRHI* rhi;
};

bool init_vulkan_engine(VulkanEngine* engine);
bool draw_frame(VulkanEngine* engine);
void vk_set_object_name(VkDevice device, uint64_t handle, VkObjectType type, const char* name);
void vk_begin_label(VkDevice device, VkCommandBuffer cb, const char* name, float r, float g, float b);
void vk_end_label(VkDevice device, VkCommandBuffer cb);
void cleanup_vulkan_engine(VulkanEngine* engine);

// IBL Module
bool init_ibl(VulkanEngine* engine);
void cleanup_ibl(VulkanEngine* engine);
void vk_ibl_bake(VulkanEngine* engine);

#endif
