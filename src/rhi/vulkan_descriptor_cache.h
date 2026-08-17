#ifndef RHI_VULKAN_DESCRIPTOR_CACHE_H
#define RHI_VULKAN_DESCRIPTOR_CACHE_H

#include "rhi_types.h"
#include <cstddef>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

class VulkanDescriptorCache {
public:
    VulkanDescriptorCache() = default;
    ~VulkanDescriptorCache() = default;

    VulkanDescriptorCache(const VulkanDescriptorCache&) = delete;
    VulkanDescriptorCache& operator=(const VulkanDescriptorCache&) = delete;
    VulkanDescriptorCache(VulkanDescriptorCache&&) = default;
    VulkanDescriptorCache& operator=(VulkanDescriptorCache&&) = default;

    VkDescriptorSetLayout GetOrCreateDescriptorSetLayout(VkDevice device, const BindGroupLayoutDesc& desc);
    VkPipelineLayout GetOrCreatePipelineLayout(VkDevice device, const std::vector<VkDescriptorSetLayout>& setLayouts, uint32_t pushConstantsSize, VkShaderStageFlags pushConstantsStages = VK_SHADER_STAGE_COMPUTE_BIT);

    void Cleanup(VkDevice device);

private:
    std::unordered_map<size_t, VkDescriptorSetLayout> m_layoutCache;
    std::unordered_map<size_t, VkPipelineLayout> m_pipelineLayoutCache;
};

#endif // RHI_VULKAN_DESCRIPTOR_CACHE_H
