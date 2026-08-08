#include "null_rhi.h"

extern "C" {
    __attribute__((visibility("default"))) IRHI* CreateRHI(struct VulkanEngine* engine) {
        (void)engine;
        return new NullRHI();
    }
    __attribute__((visibility("default"))) void DestroyRHI(IRHI* rhi) {
        delete rhi;
    }
}
