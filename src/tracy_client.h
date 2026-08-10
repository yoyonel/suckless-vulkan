#include "result.h"
#ifndef TRACY_CLIENT_H
#define TRACY_CLIENT_H

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#define SVK_TRACY_ZONE_SCOPED(name) ZoneScopedN(name)
#define SVK_TRACY_ALLOC(ptr, size) TracyAlloc(ptr, size)
#define SVK_TRACY_FREE(ptr) TracyFree(ptr)
#else
#define SVK_TRACY_ZONE_SCOPED(name)                                                                                                                            \
    do {                                                                                                                                                       \
    } while (0)
#define SVK_TRACY_ALLOC(ptr, size)                                                                                                                             \
    do {                                                                                                                                                       \
    } while (0)
#define SVK_TRACY_FREE(ptr)                                                                                                                                    \
    do {                                                                                                                                                       \
    } while (0)
#endif

AppResult tracy_client_startup(const char* programName);
AppResult tracy_client_poll_connection();
void tracy_client_mark_frame();
void tracy_client_shutdown();

#endif