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
#include "engine_config.h"
#include "engine_state.h"
#include "module_loader.h"
#include "rhi/rhi.h"
#include "rhi/rhi_ptr.h"
#include "spsc_queue.h"
#include "swapchain_init.h"

// Configuration de GLM pour Vulkan
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Phase 2B: Async HDR Loading
enum class HdrLoadRequestState : std::uint8_t {
    Pending, // Queued, waiting for I/O thread
    Loading, // I/O thread is reading file
    Ready,   // File loaded into memory, ready for GPU upload
    Failed   // Load failed (file not found, corrupt, etc.)
};

struct HdrLoadRequest {
    int hdrIndex;              // Index in hdrFiles array
    HdrLoadRequestState state; // Current load state
    uint32_t width;            // Image width
    uint32_t height;           // Image height
    uint32_t channels;         // Channels (typically 4)
    std::string sourcePathOrLabel;
    int targetIndex{-1};
    rhi::TexturePtr envHdrImage;
    rhi::SamplerPtr envHdrSampler;
    rhi::TexturePtr irradianceMap;
    rhi::TexturePtr prefilteredMap;
    VkBuffer stagingBuffer{VK_NULL_HANDLE};
    VmaAllocation stagingAllocation{VK_NULL_HANDLE};
    uint32_t envHdrMipLevels;
    bool isFallback{false};
    bool loadFailed{false};
    std::chrono::high_resolution_clock::time_point requestTime;

    HdrLoadRequest() = default;
    HdrLoadRequest(const HdrLoadRequest&) = delete;
    HdrLoadRequest& operator=(const HdrLoadRequest&) = delete;
    HdrLoadRequest(HdrLoadRequest&&) = default;
    HdrLoadRequest& operator=(HdrLoadRequest&&) = default;
};

struct HdrCleanupRequest {
    rhi::TexturePtr tex;
    rhi::SamplerPtr smp;
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};

    HdrCleanupRequest() = default;
    HdrCleanupRequest(const HdrCleanupRequest&) = delete;
    HdrCleanupRequest& operator=(const HdrCleanupRequest&) = delete;
    HdrCleanupRequest(HdrCleanupRequest&&) = default;
    HdrCleanupRequest& operator=(HdrCleanupRequest&&) = default;
};

#include "vk_engine_ibl.h"
#include "vk_renderer_context.h"

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

    RendererContext ctx;

    SwapchainManager swapchainMgr;

    VkRenderPass renderPass;
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

    // Phase 5.4: Async HDR loading infrastructure (Padding for false sharing)
    alignas(config::kCacheLineSize) struct AsyncIoState {
        SpscQueue<HdrLoadRequest, 16> hdrLoadQueue;       // Requests queued for I/O thread
        SpscQueue<HdrLoadRequest, 16> hdrReadyQueue;      // Ready/failed requests for render thread
        SpscQueue<HdrCleanupRequest, 16> hdrCleanupQueue; // Cleanup requests for I/O thread
        std::thread hdrIoThread;                          // I/O worker thread
        std::atomic<bool> hdrIoThreadRunning;             // Control flag for I/O thread
        bool hdrLoadInFlight;                             // True while worker decodes one request
        int pendingHdrIndex;                              // Last requested HDR index (-1 if none)
    } io;

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

    EngineState* appState;

    IblBaker iblBaker;
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
