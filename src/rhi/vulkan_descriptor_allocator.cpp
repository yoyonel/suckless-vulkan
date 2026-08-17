#include "vulkan_descriptor_allocator.h"

VkDescriptorPool VulkanDescriptorAllocator::CreatePool(VkDevice device, uint32_t count) {
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, count / 4},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, count},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, count / 2},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, count},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, count},
    };

    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.flags = 0;
    info.maxSets = count;
    info.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    info.pPoolSizes = poolSizes.data();

    VkDescriptorPool pool = VK_NULL_HANDLE;
    vkCreateDescriptorPool(device, &info, nullptr, &pool);
    return pool;
}

VkDescriptorSet VulkanDescriptorAllocator::Allocate(VkDevice device, VkDescriptorSetLayout layout) {
    if (m_currentPool == VK_NULL_HANDLE) {
        m_currentPool = CreatePool(device, 512);
    }

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_currentPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult res = vkAllocateDescriptorSets(device, &ai, &set);

    if (res == VK_ERROR_OUT_OF_POOL_MEMORY || res == VK_ERROR_FRAGMENTED_POOL) {
        m_usedPools.push_back(m_currentPool);
        m_currentPool = CreatePool(device, 512);
        ai.descriptorPool = m_currentPool;
        res = vkAllocateDescriptorSets(device, &ai, &set);
    }

    if (res != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return set;
}

void VulkanDescriptorAllocator::Cleanup(VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        return;
    }
    if (m_currentPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_currentPool, nullptr);
        m_currentPool = VK_NULL_HANDLE;
    }
    for (VkDescriptorPool p : m_usedPools) {
        if (p != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, p, nullptr);
        }
    }
    m_usedPools.clear();
    for (VkDescriptorPool p : m_freePools) {
        if (p != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, p, nullptr);
        }
    }
    m_freePools.clear();
}
