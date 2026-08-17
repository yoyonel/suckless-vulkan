#include "result.h"
#ifndef TRACY_CLIENT_H
#define TRACY_CLIENT_H

#include <cstdint>

namespace tracy_color {
// --- Main Frame Loop (Vert foncé parent, enfants par domaines) ---
constexpr uint32_t FrameTotal = 0x2E7D32;     // Vert forêt foncé (Total Frame)
constexpr uint32_t CpuAcquire = 0xEF6C00;     // Orange vif (Acquire Swapchain)
constexpr uint32_t CpuUpdate = 0x1976D2;      // Bleu royal (Logique & Uniforms)
constexpr uint32_t CpuUpdateChild = 0x42A5F5; // Bleu ciel (Enfant: Process Ready Texture)
constexpr uint32_t CpuRecord = 0x7B1FA2;      // Violet pourpre (RenderGraph Record)
constexpr uint32_t CpuPresent = 0x00897B;     // Sarcelle / Vert d'eau (Queue Submit & Present)

// --- Async I/O Thread (Palette Chaude : Brun foncé parent -> Oranges -> Ors) ---
constexpr uint32_t IoProcess = 0x4E342E;   // Brun espresso profond (Conteneur Parent Iteration)
constexpr uint32_t IoDecode = 0xE65100;    // Orange brûlé vif (Enfant: Décodage STB / Disque)
constexpr uint32_t IoAlloc = 0xFF8F00;     // Ambre doré (Enfant: Allocations VMA Texture)
constexpr uint32_t IoStaging = 0xF57C00;   // Mandarine (Enfant: Setup Staging Host)
constexpr uint32_t IoBakeAlloc = 0xFFA726; // Jaune orangé clair (Sous-enfant: Bake Allocations)
constexpr uint32_t IoCleanup = 0x8D6E63;   // Taupe / Brun ardoise (Nettoyage buffers)

// --- GPU Graphic Passes (Rouge Rubis parent -> Dégradé chaud / violet) ---
constexpr uint32_t GpuPass = 0xD32F2F;            // Rouge rubis (Forward Pass parent)
constexpr uint32_t GpuPassSkybox = 0xE64A19;      // Rouge orangé (Sous-passe: Skybox)
constexpr uint32_t GpuPassGeom = 0xC2185B;        // Rose magenta profond (Sous-passe: Geometry / Billboards)
constexpr uint32_t GpuPassDebug = 0xF57C00;       // Orange ambré (Sous-passe: Debug Pass)
constexpr uint32_t GpuPassPostProcess = 0x7B1FA2; // Violet pourpre (PostProcess Tonemapping)

// --- GPU Compute Passes (Or Solaire -> Ambre / Jaune) ---
constexpr uint32_t GpuCompute = 0xF57F17;     // Or solaire (Compute Shaders racine)
constexpr uint32_t GpuComputeLum = 0xFBC02D;  // Jaune d'or vif (IBL Luminance Bake)
constexpr uint32_t GpuComputeBrdf = 0xFFB300; // Ambre chaud (IBL BRDF LUT)
constexpr uint32_t GpuComputeIrr = 0xFB8C00;  // Orange ambré (IBL Irradiance Map)
constexpr uint32_t GpuComputeSpec = 0xF4511E; // Corail orangé (IBL Specular Prefilter)

// --- Pistes Virtuelles / Fibers (Async Status & Hybrid Perf) ---
constexpr uint32_t FiberAsyncIdle = 0x9E9E9E;    // Gris moyen (Async IDLE)
constexpr uint32_t FiberAsyncPending = 0xFDD835; // Jaune vif (Async PENDING)
constexpr uint32_t FiberAsyncLoading = 0x43A047; // Vert herbe (Async LOADING)
constexpr uint32_t FiberAsyncConvert = 0x00ACC1; // Cyan (Async CONVERT)
constexpr uint32_t FiberAsyncReady = 0x7CB342;   // Vert clair pomme (Async READY)
constexpr uint32_t FiberAsyncFailed = 0xE53935;  // Rouge écarlate (Async FAILED)

constexpr uint32_t FiberHybridCpu = 0x1E88E5;     // Bleu azur (Hybrid Perf: Host CPU)
constexpr uint32_t FiberHybridGpuWait = 0x8E24AA; // Violet profond (Hybrid Perf: Sync GPU Wait)

// --- Synchronisation & Cycle de Vie ---
constexpr uint32_t SyncWait = 0x607D8B;     // Gris acier (Fences / Wait Idle)
constexpr uint32_t InitShutdown = 0x263238; // Bleu nuit anthracite (Init / Shutdown)
} // namespace tracy_color

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#define SVK_TRACY_ZONE_SCOPED(name) ZoneScopedN(name)
#define SVK_TRACY_ZONE_SCOPED_C(name, color) ZoneScopedNC(name, color)
#define SVK_TRACY_ZONE_NAMED_C(varname, name, color) ZoneNamedNC(varname, name, color, true)
#define SVK_TRACY_ALLOC(ptr, size) TracyAlloc(ptr, size)
#define SVK_TRACY_FREE(ptr) TracyFree(ptr)
#define SVK_TRACY_FRAME_IMAGE(image, width, height, offset, flip) FrameImage(image, width, height, offset, flip)

#ifdef TRACY_FIBERS
namespace tracy_internal {
struct ScopedFiberGuard {
    explicit ScopedFiberGuard(const char* fiberName) {
        TracyFiberEnter(fiberName);
    }
    ~ScopedFiberGuard() {
        TracyFiberLeave;
    }
};
} // namespace tracy_internal

#define SVK_TRACY_FIBER_ZONE_C(varname, fiberName, zoneName, color)                                                                                            \
    ::tracy_internal::ScopedFiberGuard varname##_fiber(fiberName);                                                                                             \
    ZoneNamedNC(varname, zoneName, color, true)
#else
#define SVK_TRACY_FIBER_ZONE_C(varname, fiberName, zoneName, color) ((void)0)
#endif

#else
#define SVK_TRACY_ZONE_SCOPED(name) ((void)0)
#define SVK_TRACY_ZONE_SCOPED_C(name, color) ((void)0)
#define SVK_TRACY_ZONE_NAMED_C(varname, name, color) ((void)0)
#define SVK_TRACY_FIBER_ZONE_C(varname, fiberName, zoneName, color) ((void)0)
#define SVK_TRACY_ALLOC(ptr, size) ((void)0)
#define SVK_TRACY_FREE(ptr) ((void)0)
#define SVK_TRACY_FRAME_IMAGE(image, width, height, offset, flip) ((void)0)
#endif

AppResult tracy_client_startup(const char* programName);
AppResult tracy_client_poll_connection();
void tracy_client_mark_frame();
void tracy_client_shutdown(uint32_t timeout_ms = 100);

#endif