#ifndef TRACY_STATE_H
#define TRACY_STATE_H

#include "tracy_client.h"
#include <cstdint>

namespace tracy_state {

enum class AsyncState : uint8_t { Idle, Pending, Loading, Convert, Ready, Failed };

const char* to_string(AsyncState state);
uint32_t get_color(AsyncState state);
void set_async_status(AsyncState state);
AsyncState get_async_status();
void shutdown();

} // namespace tracy_state

#endif // TRACY_STATE_H
