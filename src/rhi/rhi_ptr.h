#ifndef RHI_PTR_H
#define RHI_PTR_H

#include "rhi.h"
#include <utility>

namespace rhi {

template <typename HandleT, void (IRHI::*DestroyFn)(HandleT)> class RhiHandle {
    IRHI* rhi = nullptr;
    HandleT handle = INVALID_HANDLE;

  public:
    RhiHandle() = default;

    RhiHandle(IRHI* r, HandleT h) : rhi(r), handle(h) {}

    ~RhiHandle() {
        Reset();
    }

    RhiHandle(RhiHandle&& other) noexcept : rhi(other.rhi), handle(other.handle) {
        other.handle = INVALID_HANDLE;
    }

    RhiHandle& operator=(RhiHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            rhi = other.rhi;
            handle = other.handle;
            other.handle = INVALID_HANDLE;
        }
        return *this;
    }

    void Reset(IRHI* newRhi = nullptr, HandleT newHandle = INVALID_HANDLE) {
        if (rhi && handle != INVALID_HANDLE) {
            (rhi->*DestroyFn)(handle);
        }
        rhi = newRhi;
        handle = newHandle;
    }

    // Pas de copie
    RhiHandle(const RhiHandle&) = delete;
    RhiHandle& operator=(const RhiHandle&) = delete;

    HandleT get() const {
        return handle;
    }
    operator HandleT() const {
        return handle;
    }
    bool is_valid() const {
        return handle != INVALID_HANDLE;
    }
    bool empty() const {
        return handle == INVALID_HANDLE;
    }

    // Utile pour piocher l'adresse du handle sous jacent si on passe à Vulkan (ex: &handle)
    // Mais risqué. On préfère `.get()`
};

using BufferPtr = RhiHandle<BufferHandle, &IRHI::DestroyBuffer>;
using TexturePtr = RhiHandle<TextureHandle, &IRHI::DestroyTexture>;
using ImageViewPtr = RhiHandle<ImageViewHandle, &IRHI::DestroyImageView>;
using SamplerPtr = RhiHandle<SamplerHandle, &IRHI::DestroySampler>;
using PipelinePtr = RhiHandle<PipelineHandle, &IRHI::DestroyPipeline>;
using PipelineLayoutPtr = RhiHandle<PipelineLayoutHandle, &IRHI::DestroyPipelineLayout>;
using DescriptorLayoutPtr = RhiHandle<DescriptorLayoutHandle, &IRHI::DestroyDescriptorLayout>;
using DescriptorPoolPtr = RhiHandle<DescriptorPoolHandle, &IRHI::DestroyDescriptorPool>;
using BindGroupPtr = RhiHandle<BindGroupHandle, &IRHI::DestroyBindGroup>;

} // namespace rhi

#endif // RHI_PTR_H
