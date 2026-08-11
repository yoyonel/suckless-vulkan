#ifndef SUCKLESS_VULKAN_RESULT_H
#define SUCKLESS_VULKAN_RESULT_H

#include <cstdint>

enum class [[nodiscard]] AppResult : uint8_t { Success = 0, ErrorInitializationFailed, ErrorInvalidArguments, ErrorRuntime };

enum class [[nodiscard]] RHIResult : uint8_t { Success = 0, ErrorOutOfMemory, ErrorDeviceLost, ErrorInitializationFailed, ErrorUnsupportedFeature };

enum class [[nodiscard]] ResourceResult : uint8_t { Success = 0, ErrorFileNotFound, ErrorInvalidFormat, ErrorParseFailed, ErrorCorruptedData };

enum class [[nodiscard]] GfxResult : uint8_t {
    Success = 0,
    ErrorInitializationFailed,
    ErrorInvalidState,
    ErrorOutOfMemory,
    ErrorUnsupportedFormat,
    ErrorUnsupportedFeature
};

// Helpers for logging
constexpr const char* to_string(AppResult res) {
    switch (res) {
    case AppResult::Success:
        return "Success";
    case AppResult::ErrorInitializationFailed:
        return "ErrorInitializationFailed";
    case AppResult::ErrorInvalidArguments:
        return "ErrorInvalidArguments";
    case AppResult::ErrorRuntime:
        return "ErrorRuntime";
    default:
        return "Unknown AppResult";
    }
}

constexpr const char* to_string(RHIResult res) {
    switch (res) {
    case RHIResult::Success:
        return "Success";
    case RHIResult::ErrorOutOfMemory:
        return "ErrorOutOfMemory";
    case RHIResult::ErrorDeviceLost:
        return "ErrorDeviceLost";
    case RHIResult::ErrorInitializationFailed:
        return "ErrorInitializationFailed";
    case RHIResult::ErrorUnsupportedFeature:
        return "ErrorUnsupportedFeature";
    default:
        return "Unknown RHIResult";
    }
}

constexpr const char* to_string(ResourceResult res) {
    switch (res) {
    case ResourceResult::Success:
        return "Success";
    case ResourceResult::ErrorFileNotFound:
        return "ErrorFileNotFound";
    case ResourceResult::ErrorInvalidFormat:
        return "ErrorInvalidFormat";
    case ResourceResult::ErrorParseFailed:
        return "ErrorParseFailed";
    case ResourceResult::ErrorCorruptedData:
        return "ErrorCorruptedData";
    default:
        return "Unknown ResourceResult";
    }
}

constexpr const char* to_string(GfxResult res) {
    switch (res) {
    case GfxResult::Success:
        return "Success";
    case GfxResult::ErrorInitializationFailed:
        return "ErrorInitializationFailed";
    case GfxResult::ErrorInvalidState:
        return "ErrorInvalidState";
    case GfxResult::ErrorOutOfMemory:
        return "ErrorOutOfMemory";
    case GfxResult::ErrorUnsupportedFormat:
        return "ErrorUnsupportedFormat";
    case GfxResult::ErrorUnsupportedFeature:
        return "ErrorUnsupportedFeature";
    default:
        return "Unknown GfxResult";
    }
}

#endif // SUCKLESS_VULKAN_RESULT_H
