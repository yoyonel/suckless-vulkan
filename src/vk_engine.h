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
#include "engine_state.h"
#include "module_loader.h"
#include "rhi/rhi.h"
#include "rhi/rhi_ptr.h"

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
    int hdrIndex;                                     // Index in hdrFiles array
    HdrLoadRequestState state;                        // Current load state
    VkBuffer stagingBuffer = VK_NULL_HANDLE;          // Direct mapped VRAM staging buffer
    VmaAllocation stagingAllocation = VK_NULL_HANDLE; // Allocation handle
    uint32_t width;                                   // Image width
    uint32_t height;                                  // Image height
    uint32_t channels;                                // Channels (typically 4)
    std::string sourcePathOrLabel;                    // Source file path or fallback label
};

// Phase IBL-0: Synchronous Bake Resources
struct IblResources {
    rhi::TexturePtr irradianceMap;
    rhi::SamplerPtr irradianceSampler;

    rhi::TexturePtr prefilteredMap;
    rhi::SamplerPtr prefilteredSampler;

    rhi::TexturePtr brdfLut;
    rhi::SamplerPtr brdfLutSampler;

    // Internal Compute Resources (Luminance Reduction)
    VkBuffer lumGroupSumsBuffer;
    VmaAllocation lumGroupSumsAllocation;
    VkBuffer lumMeanBuffer;
    VmaAllocation lumMeanAllocation;

    // Compute Pipelines
    rhi::PipelinePtr irmapPipeline;
    rhi::PipelinePtr spmapPipeline;
    rhi::PipelinePtr brdfLutPipeline;
    rhi::PipelinePtr lum1Pipeline;
    rhi::PipelinePtr lum2Pipeline;

    rhi::PipelineLayoutPtr iblPipelineLayout;
    rhi::PipelineLayoutPtr lum1PipelineLayout;
    rhi::PipelineLayoutPtr lum2PipelineLayout;
    rhi::DescriptorLayoutPtr iblDescriptorSetLayout;
    rhi::DescriptorLayoutPtr lum1DescriptorSetLayout;
    rhi::DescriptorLayoutPtr lum2DescriptorSetLayout;
    rhi::DescriptorPoolPtr computeDescriptorPool;

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

struct alignas(16) UBOData {
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
static_assert(sizeof(UBOData) % 16 == 0, "UBOData must be aligned to 16 bytes (std140)");

struct alignas(16) DebugPushConstant {
    glm::mat4 model;
    glm::vec4 color;
    float radius;
    int mode;
    int stippled;
    uint32_t _padding;
};
static_assert(sizeof(DebugPushConstant) % 16 == 0, "DebugPushConstant must be padded to a multiple of 16 bytes");

struct VulkanEngine {

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
    rhi::TexturePtr depthImage;
    VkFormat depthFormat;

    // NOUVEAU : Le Layout de notre descripteur
    rhi::DescriptorLayoutPtr globalDescriptorLayout;

    rhi::PipelineLayoutPtr pipelineLayout;
    rhi::PipelineLayoutPtr debugPipelineLayout;
    rhi::PipelinePtr graphicsPipeline;
    rhi::PipelinePtr billboardPipeline;
    rhi::PipelinePtr wireframePipeline;
    rhi::PipelinePtr debugLinePipeline;
    rhi::PipelinePtr debugTrianglePipeline;
    rhi::PipelinePtr skyboxPipeline;

    rhi::BufferPtr vertexBuffer;
    rhi::BufferPtr indexBuffer;
    uint32_t indexCount;

    // Instancing : Flat Transform Buffer (SSBO)
    rhi::BufferPtr transformBuffer;
    void* transformBufferMapped{nullptr};

    // Buffer (SSBO) pour stocker les 100 matériaux PBR
    rhi::BufferPtr materialBuffer;

    // Notre Uniform Buffer et son mapping persistant
    rhi::BufferPtr uniformBuffer;
    void* uniformBufferMapped;

    // NOUVEAU : Le pool et le set de descripteurs
    rhi::DescriptorPoolPtr globalDescriptorPool;
    DescriptorSetHandle descriptorSet{INVALID_HANDLE};

    rhi::TexturePtr envHdrImage;
    rhi::SamplerPtr envHdrSampler;
    uint32_t envHdrMipLevels;
    uint32_t envHdrWidth;
    uint32_t envHdrHeight;
    std::vector<std::string> hdrFiles;
    int currentHdrIndex;

#define CACHE_LINE_SIZE 128

    // Phase 2B: Async HDR loading infrastructure
    std::queue<HdrLoadRequest> hdrLoadQueue;          // Requests queued for I/O thread
    std::queue<HdrLoadRequest> hdrReadyQueue;         // Ready/failed requests for render thread
    std::thread hdrIoThread;                          // I/O worker thread
    alignas(CACHE_LINE_SIZE) std::mutex hdrLoadMutex; // Protect queue state
    std::condition_variable hdrLoadCV;                // Signal I/O thread on new requests
    bool hdrIoThreadRunning;                          // Control flag for I/O thread
    bool hdrLoadInFlight;                             // True while worker decodes one request
    int pendingHdrIndex;                              // Last requested HDR index (-1 if none)

    alignas(CACHE_LINE_SIZE) VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;

    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;
    uint32_t lastRenderedImageIndex;
    void* tracyVkContext;

    rhi::BufferPtr billboardBuffer;
    rhi::BufferPtr billboardPosSSBO;
    rhi::BufferPtr billboardMatSSBO;
    void* billboardMapped;

    IblResources ibl;

    EngineState* appState;
};

GfxResult init_vulkan_engine(VulkanEngine* engine);
GfxResult draw_frame(VulkanEngine* engine);
void vk_set_object_name(VkDevice device, uint64_t handle, VkObjectType type, const char* name);
void vk_begin_label(VkDevice device, VkCommandBuffer cb, const char* name, float r, float g, float b);
void vk_end_label(VkDevice device, VkCommandBuffer cb);
void cleanup_vulkan_engine(VulkanEngine* engine);

// IBL Module
GfxResult init_ibl(VulkanEngine* engine);
void cleanup_ibl(VulkanEngine* engine);
void vk_ibl_bake(VulkanEngine* engine);

#endif
