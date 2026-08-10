#ifndef TRACY_VULKAN_H
#define TRACY_VULKAN_H

#include "result.h"
#include "vk_engine.h"

#ifdef TRACY_ENABLE
#include "rhi/rhi.h"
#include <tracy/TracyVulkan.hpp>
#define SVK_TRACY_VK_NAMED_ZONE(varname, engine, commandBuffer, name)                                                                                          \
    TracyVkNamedZone(static_cast<TracyVkCtx>((engine)->tracyVkContext), varname, commandBuffer, name, true)
#define SVK_RHI_GPU_ZONE(varname, rhi, name)                                                                                                                   \
    TracyVkNamedZone(static_cast<TracyVkCtx>((rhi)->GetOpaqueTracyContext()), varname, static_cast<VkCommandBuffer>((rhi)->GetOpaqueCommandBuffer()), name,    \
                     true)
#else
#define SVK_TRACY_VK_NAMED_ZONE(varname, engine, commandBuffer, name)
#define SVK_RHI_GPU_ZONE(varname, rhi, name)
#endif

GfxResult tracy_vk_context_init(VulkanEngine* engine);
void tracy_vk_context_destroy(VulkanEngine* engine);
void tracy_vk_collect(VulkanEngine* engine, VkCommandBuffer commandBuffer);

#endif