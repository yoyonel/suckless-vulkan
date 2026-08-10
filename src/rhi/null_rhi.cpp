#include "null_rhi.h"
#include "../engine_state.h"

#include <new>

extern "C" {
    __attribute__((visibility("default"))) IRHI* CreateRHI(EngineState* state) {
        state->rhiArena.offset = 0; // Reset arena on load
        void* rhiMem = arena_alloc(&state->rhiArena, sizeof(NullRHI), alignof(NullRHI));
        return new(rhiMem) NullRHI();
    }
    __attribute__((visibility("default"))) void DestroyRHI(IRHI* rhi) {
        NullRHI* nRhi = static_cast<NullRHI*>(rhi);
        nRhi->~NullRHI();
    }
}
