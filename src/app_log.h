#ifndef APP_LOG_H
#define APP_LOG_H

#include <cstdarg>
#include <cstdint>

// Log levels are ordered by severity.
enum class LogLevel : std::uint8_t {
    NotSet = 0,
    Debug = 10,
    Info = 20,
    Warning = 30,
    Error = 40,
    Critical = 50,
};

using LogCallback = void (*)(LogLevel level, const char* tag, const char* message);

void log_set_level(LogLevel level);
LogLevel log_get_level();
void log_set_callback(LogCallback callback);
void log_message(LogLevel level, const char* tag, const char* format, ...);
void log_message_v(LogLevel level, const char* tag, const char* format, va_list args);

#define LOG_DEBUG(TAG, FMT, ...) log_message(LogLevel::Debug, TAG, FMT, ##__VA_ARGS__)
#define LOG_INFO(TAG, FMT, ...) log_message(LogLevel::Info, TAG, FMT, ##__VA_ARGS__)
#define LOG_WARNING(TAG, FMT, ...) log_message(LogLevel::Warning, TAG, FMT, ##__VA_ARGS__)
#define LOG_ERROR(TAG, FMT, ...) log_message(LogLevel::Error, TAG, FMT, ##__VA_ARGS__)
#define LOG_CRITICAL(TAG, FMT, ...) log_message(LogLevel::Critical, TAG, FMT, ##__VA_ARGS__)

#endif
