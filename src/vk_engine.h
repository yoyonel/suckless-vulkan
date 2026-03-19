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
    // Baked Images
    VkImage irradianceMap;
    VmaAllocation irradianceMapAllocation;
    VkImageView irradianceMapView;
    VkSampler irradianceSampler;

    VkImage prefilteredMap;
    VmaAllocation prefilteredMapAllocation;
    VkImageView prefilteredMapView;
    VkSampler prefilteredSampler;

    VkImage brdfLut;
    VmaAllocation brdfLutAllocation;
    VkImageView brdfLutView;
    VkSampler brdfLutSampler;

    // Internal Compute Resources (Luminance Reduction)
    VkBuffer lumGroupSumsBuffer;
    VmaAllocation lumGroupSumsAllocation;
    VkBuffer lumMeanBuffer;
    VmaAllocation lumMeanAllocation;

    // Compute Pipelines
    VkPipeline irmapPipeline;
    VkPipeline spmapPipeline;
    VkPipeline brdfLutPipeline;
    VkPipeline lum1Pipeline;
    VkPipeline lum2Pipeline;

    VkPipelineLayout iblPipelineLayout;
    VkPipelineLayout lum1PipelineLayout;
    VkPipelineLayout lum2PipelineLayout;
    VkDescriptorSetLayout iblDescriptorSetLayout;
    VkDescriptorSetLayout lum1DescriptorSetLayout;
    VkDescriptorSetLayout lum2DescriptorSetLayout;
    VkDescriptorPool computeDescriptorPool;

    // Descriptor sets for individual compute passes
    VkDescriptorSet lum1DescriptorSet;
    VkDescriptorSet lum2DescriptorSet;
    VkDescriptorSet irmapDescriptorSet;
    VkDescriptorSet spmapDescriptorSet;
    VkDescriptorSet brdfLutDescriptorSet;

    bool brdfLutBaked;
    float bakedMeanLuminance;
};

typedef struct {
    float position[3];
    float color[3];
} Vertex;

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
    glm::vec2 windowSize;
};

struct DebugPushConstant {
    glm::mat4 model;
    glm::vec4 color;
    float radius;
    int mode;
    int stippled;
};

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
    VkPipelineLayout debugPipelineLayout;
    VkPipeline graphicsPipeline;
    VkPipeline billboardPipeline;
    VkPipeline wireframePipeline;
    VkPipeline debugLinePipeline;
    VkPipeline debugTrianglePipeline;
    VkPipeline skyboxPipeline;

    VkBuffer vertexBuffer;
    VmaAllocation vertexBufferAllocation;
    VkBuffer indexBuffer;
    VmaAllocation indexBufferAllocation;
    uint32_t indexCount;

    // Instancing : buffer contenant les positions des 100 sphères
    VkBuffer instanceBuffer;
    VmaAllocation instanceBufferAllocation;

    // Buffer (SSBO) pour stocker les 100 matériaux PBR
    VkBuffer materialBuffer;
    VmaAllocation materialBufferAllocation;

    // Notre Uniform Buffer et son mapping persistant
    VkBuffer uniformBuffer;
    VmaAllocation uniformBufferAllocation;
    void* uniformBufferMapped;

    // NOUVEAU : Le pool et le set de descripteurs
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;

    VkImage envHdrImage;
    VmaAllocation envHdrImageAllocation;
    VkImageView envHdrImageView;
    VkSampler envHdrSampler;
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

    float animationTimeSeconds;
    float animationSpeed;
    bool animationPaused;
    bool pauseKeyWasDown;
    bool resetKeyWasDown;
    bool speedUpKeyWasDown;
    bool speedDownKeyWasDown;
    bool fullscreenKeyWasDown;
    bool escapeKeyWasDown;
    bool isFullscreen;
    bool cameraToggleKeyWasDown;
    bool showEnvmapToggleKeyWasDown;
    bool envPageUpKeyWasDown;
    bool envPageDownKeyWasDown;
    bool cameraEnabled;
    bool showEnvmap;
    float envLod;
    int iblDebugMode;
    float iblDebugScale;
    bool iblDebugDigitKeyWasDown[10];
    bool iblDebugPrevKeyWasDown;
    bool iblDebugNextKeyWasDown;
    bool iblExportKeyWasDown;
    bool iblDebugF5KeyWasDown;
    bool cameraResetKeyWasDown;
    bool postResetKeyWasDown;
    bool postExposureAddKeyWasDown;
    bool postExposureSubKeyWasDown;
    int windowedPosX;
    int windowedPosY;
    int windowedWidth;
    int windowedHeight;
    float lastFrameDeltaSeconds;

    bool billboardMode;
    bool billboardKeyWasDown;
    bool wireframeMode;
    bool wireframeKeyWasDown;

    Camera camera;
    std::chrono::steady_clock::time_point lastFrameTimestamp;

    IblResources ibl;
    bool pbrEnabled;
    bool iblEnabled;
    float iblIntensity;

    // Post-processing parameters (Legacy Parity)
    float exposure;
    float saturation;
    float contrast;
    float gamma;
    float gain;
    float offset;
    float wbTemp;
    float wbTint;

} VulkanEngine;

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
