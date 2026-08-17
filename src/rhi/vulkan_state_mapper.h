#ifndef RHI_VULKAN_STATE_MAPPER_H
#define RHI_VULKAN_STATE_MAPPER_H

#include "rhi_types.h"
#include <vulkan/vulkan.h>

struct VulkanStateMapping {
    VkImageLayout layout;
    VkAccessFlags accessMask;
    VkPipelineStageFlags stageMask;
};

VulkanStateMapping map_resource_state_to_vulkan(ResourceState state);

#endif // RHI_VULKAN_STATE_MAPPER_H
