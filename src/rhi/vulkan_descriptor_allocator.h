#ifndef RHI_VULKAN_DESCRIPTOR_ALLOCATOR_H
#define RHI_VULKAN_DESCRIPTOR_ALLOCATOR_H

#include <vector>
#include <vulkan/vulkan.h>

class VulkanDescriptorAllocator {
public:
    VulkanDescriptorAllocator() = default;
    ~VulkanDescriptorAllocator() = default;

    VulkanDescriptorAllocator(const VulkanDescriptorAllocator&) = delete;
    VulkanDescriptorAllocator& operator=(const VulkanDescriptorAllocator&) = delete;
    VulkanDescriptorAllocator(VulkanDescriptorAllocator&&) = default;
    VulkanDescriptorAllocator& operator=(VulkanDescriptorAllocator&&) = default;

    VkDescriptorSet Allocate(VkDevice device, VkDescriptorSetLayout layout);
    void Cleanup(VkDevice device);

private:
    static VkDescriptorPool CreatePool(VkDevice device, uint32_t count);

    VkDescriptorPool m_currentPool{VK_NULL_HANDLE};
    std::vector<VkDescriptorPool> m_usedPools;
    std::vector<VkDescriptorPool> m_freePools;
};

#endif // RHI_VULKAN_DESCRIPTOR_ALLOCATOR_H
