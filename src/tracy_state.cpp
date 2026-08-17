#include "tracy_state.h"
#include <atomic>
#include <mutex>
#ifdef TRACY_ENABLE
#include <tracy/TracyC.h>
#endif

namespace tracy_state {

static std::atomic<AsyncState> g_asyncState{AsyncState::Idle};

#if defined(TRACY_ENABLE) && defined(TRACY_FIBERS)
static std::mutex s_stateMutex;
static TracyCZoneCtx s_activeFiberZone{};
static bool s_hasActiveZone = false;

static const struct ___tracy_source_location_data s_srcloc_idle = {"Async IDLE", "Async Status", __FILE__, (uint32_t)__LINE__, tracy_color::FiberAsyncIdle};
static const struct ___tracy_source_location_data s_srcloc_pending = {"Async PENDING", "Async Status", __FILE__, (uint32_t)__LINE__,
                                                                      tracy_color::FiberAsyncPending};
static const struct ___tracy_source_location_data s_srcloc_loading = {"Async LOADING", "Async Status", __FILE__, (uint32_t)__LINE__,
                                                                      tracy_color::FiberAsyncLoading};
static const struct ___tracy_source_location_data s_srcloc_convert = {"Async CONVERT", "Async Status", __FILE__, (uint32_t)__LINE__,
                                                                      tracy_color::FiberAsyncConvert};
static const struct ___tracy_source_location_data s_srcloc_ready = {"Async READY", "Async Status", __FILE__, (uint32_t)__LINE__, tracy_color::FiberAsyncReady};
static const struct ___tracy_source_location_data s_srcloc_failed = {"Async FAILED", "Async Status", __FILE__, (uint32_t)__LINE__,
                                                                     tracy_color::FiberAsyncFailed};
#endif

const char* to_string(AsyncState state) {
    switch (state) {
    case AsyncState::Idle:
        return "Async IDLE";
    case AsyncState::Pending:
        return "Async PENDING";
    case AsyncState::Loading:
        return "Async LOADING";
    case AsyncState::Convert:
        return "Async CONVERT";
    case AsyncState::Ready:
        return "Async READY";
    case AsyncState::Failed:
        return "Async FAILED";
    default:
        return "Async UNKNOWN";
    }
}

uint32_t get_color(AsyncState state) {
    switch (state) {
    case AsyncState::Idle:
        return tracy_color::FiberAsyncIdle;
    case AsyncState::Pending:
        return tracy_color::FiberAsyncPending;
    case AsyncState::Loading:
        return tracy_color::FiberAsyncLoading;
    case AsyncState::Convert:
        return tracy_color::FiberAsyncConvert;
    case AsyncState::Ready:
        return tracy_color::FiberAsyncReady;
    case AsyncState::Failed:
        return tracy_color::FiberAsyncFailed;
    default:
        return 0xFFFFFF;
    }
}

void set_async_status(AsyncState state) {
#if defined(TRACY_ENABLE) && defined(TRACY_FIBERS)
    std::lock_guard<std::mutex> lock(s_stateMutex);
    AsyncState oldState = g_asyncState.load(std::memory_order_relaxed);
    if (oldState == state && s_hasActiveZone) {
        return;
    }
    g_asyncState.store(state, std::memory_order_release);

    TracyFiberEnter("Async Status");
    if (s_hasActiveZone) {
        TracyCZoneEnd(s_activeFiberZone);
        s_hasActiveZone = false;
    }

    const struct ___tracy_source_location_data* srcloc = nullptr;
    switch (state) {
    case AsyncState::Idle:
        srcloc = &s_srcloc_idle;
        break;
    case AsyncState::Pending:
        srcloc = &s_srcloc_pending;
        break;
    case AsyncState::Loading:
        srcloc = &s_srcloc_loading;
        break;
    case AsyncState::Convert:
        srcloc = &s_srcloc_convert;
        break;
    case AsyncState::Ready:
        srcloc = &s_srcloc_ready;
        break;
    case AsyncState::Failed:
        srcloc = &s_srcloc_failed;
        break;
    }

    if (srcloc) {
        s_activeFiberZone = ___tracy_emit_zone_begin_callstack(srcloc, TRACY_CALLSTACK, 1);
        s_hasActiveZone = true;
    }
    TracyFiberLeave;
#else
    g_asyncState.store(state, std::memory_order_release);
#endif
}

AsyncState get_async_status() {
    return g_asyncState.load(std::memory_order_acquire);
}

void shutdown() {
#if defined(TRACY_ENABLE) && defined(TRACY_FIBERS)
    std::lock_guard<std::mutex> lock(s_stateMutex);
    if (s_hasActiveZone) {
        TracyFiberEnter("Async Status");
        TracyCZoneEnd(s_activeFiberZone);
        s_hasActiveZone = false;
        TracyFiberLeave;
    }
#endif
}

} // namespace tracy_state
