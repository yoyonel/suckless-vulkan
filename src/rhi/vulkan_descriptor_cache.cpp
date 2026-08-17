#include "vulkan_descriptor_cache.h"
#include <algorithm>

namespace {

VkDescriptorType map_binding_type_to_vk(BindingType type) {
    switch (type) {
    case BindingType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case BindingType::SampledTexture:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case BindingType::CombinedImageSampler:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case BindingType::StorageTexture:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case BindingType::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingType::UniformBufferDynamic:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    case BindingType::StorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingType::StorageBufferDynamic:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    case BindingType::InputAttachment:
        return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    }
    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

size_t hash_combine(size_t seed, size_t val) {
    return seed ^ (val + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

size_t hash_bind_group_layout_desc(const BindGroupLayoutDesc& desc) {
    size_t hash = 0x811c9dc5;
    for (const auto& b : desc.bindings) {
        hash = hash_combine(hash, static_cast<size_t>(b.binding));
        hash = hash_combine(hash, static_cast<size_t>(b.type));
        hash = hash_combine(hash, static_cast<size_t>(b.count));
        hash = hash_combine(hash, static_cast<size_t>(b.stageFlags));
    }
    return hash;
}

size_t hash_pipeline_layout(const std::vector<VkDescriptorSetLayout>& layouts, uint32_t pushConstantsSize, VkShaderStageFlags stages) {
    size_t hash = 0xcbf29ce484222325ULL;
    for (VkDescriptorSetLayout l : layouts) {
        hash = hash_combine(hash, reinterpret_cast<size_t>(l));
    }
    hash = hash_combine(hash, static_cast<size_t>(pushConstantsSize));
    hash = hash_combine(hash, static_cast<size_t>(stages));
    return hash;
}

} // namespace

VkDescriptorSetLayout VulkanDescriptorCache::GetOrCreateDescriptorSetLayout(VkDevice device, const BindGroupLayoutDesc& desc) {
    size_t key = hash_bind_group_layout_desc(desc);
    auto it = m_layoutCache.find(key);
    if (it != m_layoutCache.end()) {
        return it->second;
    }

    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    vkBindings.reserve(desc.bindings.size());

    for (const auto& b : desc.bindings) {
        VkDescriptorSetLayoutBinding vb{};
        vb.binding = b.binding;
        vb.descriptorType = map_binding_type_to_vk(b.type);
        vb.descriptorCount = b.count > 0 ? b.count : 1;
        vb.stageFlags = b.stageFlags != 0 ? static_cast<VkShaderStageFlags>(b.stageFlags) : static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_COMPUTE_BIT);
        vb.pImmutableSamplers = nullptr;
        vkBindings.push_back(vb);
    }

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(vkBindings.size());
    info.pBindings = vkBindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    m_layoutCache[key] = layout;
    return layout;
}

VkPipelineLayout VulkanDescriptorCache::GetOrCreatePipelineLayout(VkDevice device, const std::vector<VkDescriptorSetLayout>& setLayouts, uint32_t pushConstantsSize, VkShaderStageFlags pushConstantsStages) {
    size_t key = hash_pipeline_layout(setLayouts, pushConstantsSize, pushConstantsStages);
    auto it = m_pipelineLayoutCache.find(key);
    if (it != m_pipelineLayoutCache.end()) {
        return it->second;
    }

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = pushConstantsStages;
    pcRange.offset = 0;
    pcRange.size = pushConstantsSize;

    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    info.pSetLayouts = setLayouts.empty() ? nullptr : setLayouts.data();
    info.pushConstantRangeCount = pushConstantsSize > 0 ? 1 : 0;
    info.pPushConstantRanges = pushConstantsSize > 0 ? &pcRange : nullptr;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device, &info, nullptr, &layout) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    m_pipelineLayoutCache[key] = layout;
    return layout;
}

void VulkanDescriptorCache::Cleanup(VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        return;
    }
    for (auto& pair : m_pipelineLayoutCache) {
        if (pair.second != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pair.second, nullptr);
        }
    }
    m_pipelineLayoutCache.clear();

    for (auto& pair : m_layoutCache) {
        if (pair.second != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, pair.second, nullptr);
        }
    }
    m_layoutCache.clear();
}
