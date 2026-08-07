#include "tracy_client.h"

#ifdef TRACY_ENABLE

#include "app_log.h"
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>

bool g_tracyInitialized = false;

bool tracy_client_startup(const char* programName) {
    if (g_tracyInitialized) {
        LOG_WARNING("tracy", "Tracy client deja initialise.");
        return true;
    }

    if (programName != nullptr && programName[0] != '\0') {
        TracySetProgramName(programName);
        LOG_INFO("tracy", "Tracy enregistre : %s", programName);
    }

    TracyCSetThreadName("Main Thread");
    LOG_INFO("tracy", "Tracy client initialise (auto-lifecycle).");
    g_tracyInitialized = true;
    return true;
}

bool tracy_client_poll_connection() {
    if (!g_tracyInitialized) {
        return true;
    }
    // With automatic lifecycle, no explicit connection polling needed
    return true;
}

void tracy_client_mark_frame() {
    if (g_tracyInitialized) {
        TracyCFrameMark;
    }
}

void tracy_client_shutdown() {
    if (!g_tracyInitialized) {
        return;
    }

    LOG_INFO("tracy", "Tracy client shutdown (Tracy will cleanup via static destructors).");
    g_tracyInitialized = false;
    // No explicit ShutdownProfiler() call - Tracy handles cleanup automatically
}

#else

// Mock implementations when TRACY_ENABLE not defined

bool tracy_client_startup(const char* programName) {
    (void)programName;
    return true;
}

bool tracy_client_poll_connection() {
    return true;
}

void tracy_client_mark_frame() {}

void tracy_client_shutdown() {}

#endif