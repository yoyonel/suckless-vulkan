#ifndef VK_ENGINE_IBL_H
#define VK_ENGINE_IBL_H

#include "vk_engine.h"

// IBL Resolution Constants
constexpr uint32_t IBL_IRM_SIZE = 64;
constexpr uint32_t IBL_SPM_SIZE = 1024;
constexpr uint32_t IBL_SPM_MIPS = 11;
constexpr uint32_t IBL_BRDF_SIZE = 512;

// Luminance Reduction Constants
constexpr uint32_t IBL_LUM_WG_SIZE = 16;
constexpr uint32_t IBL_MAX_GROUPS = 65536;

void vk_ibl_bake_luminance(VulkanEngine* engine);
void vk_ibl_bake_brdf(VulkanEngine* engine);
void vk_ibl_bake_irradiance(VulkanEngine* engine);
void vk_ibl_bake_prefilter(VulkanEngine* engine);
void vk_ibl_export_maps(VulkanEngine* engine);

#endif // VK_ENGINE_IBL_H
