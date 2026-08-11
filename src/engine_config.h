#ifndef ENGINE_CONFIG_H
#define ENGINE_CONFIG_H

#include <cstddef>
#include <cstdint>

namespace config {
constexpr uint32_t kGridSize = 10;
constexpr float kGridSpacingMeters = 2.5f;
constexpr float kGridOffset = (static_cast<float>(kGridSize) - 1.0f) * 0.5f * kGridSpacingMeters;
constexpr size_t kMaterialInstanceCount = static_cast<size_t>(kGridSize) * static_cast<size_t>(kGridSize);

constexpr const char* kMaterialJsonPath = "assets/materials/pbr_materials.json";
constexpr int kLegacyWindowWidth = 1024;
constexpr int kLegacyWindowHeight = 768;

constexpr float kDefaultMaterialRoughness = 0.5f;
constexpr float kDefaultMaterialAo = 1.0f;

constexpr uint32_t kMaxSwapchainImages = 8;
constexpr size_t kCacheLineSize = 128;
} // namespace config

#endif
