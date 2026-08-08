#ifndef VULKAN_RHI_H
#define VULKAN_RHI_H

#include "rhi.h"
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

class VulkanRHI : public IRHI {
public:
    VulkanRHI(struct VulkanEngine* engine);
    ~VulkanRHI() override;

    bool Init() override;
    void Shutdown() override;

    BufferHandle CreateBuffer(std::size_t size, BufferUsage usage, const void* initialData = nullptr, const char* name = nullptr) override;
    void DestroyBuffer(BufferHandle handle) override;
    void* MapBuffer(BufferHandle handle) override;
    void UnmapBuffer(BufferHandle handle) override;

    VkBuffer GetVkBuffer(BufferHandle handle) const;

    TextureHandle CreateTexture(uint32_t width, uint32_t height, TextureFormat format, TextureUsage usage, uint32_t mipLevels = 1, const char* name = nullptr) override;
    void DestroyTexture(TextureHandle handle) override;

    ImageViewHandle CreateImageView(TextureHandle texture, uint32_t baseMipLevel = 0, uint32_t levelCount = 1, uint32_t baseArrayLayer = 0, uint32_t layerCount = 1) override;
    void DestroyImageView(ImageViewHandle handle) override;

    SamplerHandle CreateSampler(uint32_t mipLevels = 1, bool clampToEdge = false, const char* name = nullptr) override;
    void DestroySampler(SamplerHandle handle) override;

    DescriptorLayoutHandle CreateDescriptorLayout(const DescriptorLayoutDesc& desc, const char* name = nullptr) override;
    void DestroyDescriptorLayout(DescriptorLayoutHandle handle) override;

    DescriptorPoolHandle CreateDescriptorPool(const DescriptorPoolDesc& desc, const char* name = nullptr) override;
    void DestroyDescriptorPool(DescriptorPoolHandle handle) override;

    bool AllocateDescriptorSets(const DescriptorSetAllocateDesc& desc, DescriptorSetHandle* outSets) override;

    void UpdateDescriptorSets(uint32_t writeCount, const WriteDescriptorSet* pDescriptorWrites) override;
    PipelineLayoutHandle CreatePipelineLayout(const PipelineLayoutDesc& desc, const char* name = nullptr) override;
    void DestroyPipelineLayout(PipelineLayoutHandle handle) override;

    PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc) override;
    PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    void DestroyPipeline(PipelineHandle handle) override;
    VkImage GetVkImage(TextureHandle handle) const;
    VkImageView GetVkImageView(TextureHandle handle) const;
    VkImageView GetVkImageViewForHandle(ImageViewHandle handle) const;
    VkSampler GetVkSampler(SamplerHandle handle) const;
    VkDescriptorSetLayout GetVkDescriptorSetLayout(DescriptorLayoutHandle handle) const;
    VkDescriptorPool GetVkDescriptorPool(DescriptorPoolHandle handle) const;
    VkDescriptorSet GetVkDescriptorSet(DescriptorSetHandle handle) const;
    VkPipelineLayout GetVkPipelineLayout(PipelineLayoutHandle handle) const;
    VkPipeline GetVkPipeline(PipelineHandle handle) const;

    bool BeginFrame() override;
    void EndFrame() override;

    SwapchainStatus AcquireNextImage(uint32_t* imageIndex) override;
    void UpdateUBO(const struct UBOData& data) override;
    SwapchainStatus SubmitAndPresent(uint32_t imageIndex) override;

    void BeginRenderPass() override;
    void EndRenderPass() override;
    void CmdBindPipeline(CommandBufferHandle cb, PipelineHandle pipeline, bool isCompute = false) override;
    void CmdBindDescriptorSets(CommandBufferHandle cb, PipelineLayoutHandle layout, uint32_t firstSet, uint32_t count, const DescriptorSetHandle* sets, bool isCompute = false) override;
    void CmdPushConstants(CommandBufferHandle cb, PipelineLayoutHandle layout, ShaderStage stage, uint32_t offset, uint32_t size, const void* values) override;
    void CmdDraw(CommandBufferHandle cb, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
    void CmdDrawIndexed(CommandBufferHandle cb, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) override;
    void CmdDispatch(CommandBufferHandle cb, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;

    // Direct Vulkan commands wrapper for IBL/EnvMap
    void CmdPipelineBarrier(VkCommandBuffer cb, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers);
    void CmdCopyImageToBuffer(VkCommandBuffer cb, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions);
    void CmdCopyBufferToImage(VkCommandBuffer cb, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions);
    void CmdBlitImage(VkCommandBuffer cb, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter);

    void BindPipeline(PipelineType type) override;
    void BindGlobalDescriptor() override;
    void BindMeshBuffers(bool isBillboard) override;

    void Draw(uint32_t vertexCount, uint32_t instanceCount) override;
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount) override;
    void UpdateBillboardInstances(const struct BillboardInstance* instances, std::size_t count) override;
    
    void PushDebugConstants(const void* data, uint32_t size) override;
    void BeginDebugLabel(const char* name, float r, float g, float b) override;
    void EndDebugLabel() override;
    void CollectProfiling() override;
    void GetResolution(uint32_t* width, uint32_t* height) const override;
    void* GetOpaqueTracyContext() const override;
    void* GetOpaqueCommandBuffer() const override;

private:
    struct VulkanBuffer {
        VkBuffer buffer{VK_NULL_HANDLE};
        VmaAllocation allocation{VK_NULL_HANDLE};
        void* mappedData{nullptr};
    };

    struct VulkanTexture {
        VkImage image{VK_NULL_HANDLE};
        VkImageView imageView{VK_NULL_HANDLE};
        VmaAllocation allocation{VK_NULL_HANDLE};
        VkFormat format{VK_FORMAT_UNDEFINED};
    };

    struct VulkanImageView {
        VkImageView view{VK_NULL_HANDLE};
    };

    struct VulkanSampler {
        VkSampler sampler{VK_NULL_HANDLE};
    };

    std::vector<VulkanBuffer> m_buffers;
    uint32_t m_nextBufferHandle{1};

    std::vector<VulkanTexture> m_textures;
    uint32_t m_nextTextureHandle{1};

    std::vector<VulkanImageView> m_imageViews;
    uint32_t m_nextImageViewHandle{1};

    std::vector<VulkanSampler> m_samplers;
    uint32_t m_nextSamplerHandle{1};

    struct VulkanDescriptorLayout {
        VkDescriptorSetLayout layout{VK_NULL_HANDLE};
    };
    std::vector<VulkanDescriptorLayout> m_descriptorLayouts;
    uint32_t m_nextDescriptorLayoutHandle{1};

    struct VulkanDescriptorPool {
        VkDescriptorPool pool{VK_NULL_HANDLE};
    };
    std::vector<VulkanDescriptorPool> m_descriptorPools;
    std::vector<VkPipelineLayout> m_pipelineLayouts;
    std::vector<VkPipeline> m_pipelines;
    uint32_t m_nextDescriptorPoolHandle{1};

    struct VulkanDescriptorSet {
        VkDescriptorSet set{VK_NULL_HANDLE};
    };
    std::vector<VulkanDescriptorSet> m_descriptorSets;
    uint32_t m_nextDescriptorSetHandle{1};

    struct VulkanEngine* _engine;
};

#endif
