#ifndef TRACY_CLIENT_H
#define TRACY_CLIENT_H

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#define SVK_TRACY_ZONE_SCOPED(name) ZoneScopedN(name)
#else
#define SVK_TRACY_ZONE_SCOPED(name)
#endif

bool tracy_client_startup(const char* programName);
bool tracy_client_poll_connection();
void tracy_client_mark_frame();
void tracy_client_shutdown();

#endif