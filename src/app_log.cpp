#include "app_log.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace {

constexpr size_t TIME_BUFFER_SIZE = 24;
constexpr size_t PREFIX_BUFFER_SIZE = 160;

LogLevel g_log_level = LogLevel::Info;
bool g_log_initialized = false;
LogCallback g_log_callback = nullptr;
std::mutex g_log_mutex;

const char* level_to_string(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARNING";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Critical:
        return "CRITICAL";
    default:
        return "UNKNOWN";
    }
}

LogLevel string_to_level(const char* str) {
    if (str == nullptr || str[0] == '\0') {
        return LogLevel::NotSet;
    }

    std::string value(str);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (value == "DEBUG") {
        return LogLevel::Debug;
    }
    if (value == "INFO") {
        return LogLevel::Info;
    }
    if (value == "WARNING" || value == "WARN") {
        return LogLevel::Warning;
    }
    if (value == "ERROR") {
        return LogLevel::Error;
    }
    if (value == "CRITICAL") {
        return LogLevel::Critical;
    }

    return LogLevel::NotSet;
}

void ensure_log_initialized_locked() {
    if (g_log_initialized) {
        return;
    }

    const char* env_level = std::getenv("VULKAN_LOG_LEVEL");
    if (env_level == nullptr || env_level[0] == '\0') {
        env_level = std::getenv("OGL_LOG_LEVEL");
    }

    const LogLevel parsed_level = string_to_level(env_level);
    if (parsed_level != LogLevel::NotSet) {
        g_log_level = parsed_level;
    }

    g_log_initialized = true;
}

int32_t get_process_id() {
#if defined(__unix__) || defined(__APPLE__)
    return static_cast<int32_t>(::getpid());
#else
    return 0;
#endif
}

void get_local_time(std::time_t seconds, std::tm* out_tm) {
#if defined(_WIN32)
    localtime_s(out_tm, &seconds);
#else
    localtime_r(&seconds, out_tm);
#endif
}

} // namespace

void log_set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_level = level;
    g_log_initialized = true;
}

LogLevel log_get_level() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    ensure_log_initialized_locked();
    return g_log_level;
}

void log_set_callback(LogCallback callback) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_callback = callback;
}

void log_message(LogLevel level, const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_message_v(level, tag, format, args);
    va_end(args);
}

void log_message_v(LogLevel level, const char* tag, const char* format, va_list args) {
    LogCallback callback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        ensure_log_initialized_locked();
        if (level < g_log_level) {
            return;
        }
        callback = g_log_callback;
    }

    const auto now = std::chrono::system_clock::now();
    const auto epoch = now.time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count() % 1000;
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);

    std::tm local_tm{};
    get_local_time(seconds, &local_tm);

    char time_buf[TIME_BUFFER_SIZE] = {};
    (void)std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &local_tm);

    const auto tid_hash = static_cast<unsigned long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    const int32_t pid = get_process_id();

    char prefix[PREFIX_BUFFER_SIZE] = {};
    (void)std::snprintf(prefix, sizeof(prefix), "%s,%03lld [%d:%llu] - %s - %-8s - ", time_buf, static_cast<long long>(millis), pid, tid_hash,
                        tag != nullptr ? tag : "app", level_to_string(level));

    va_list args_copy;
    va_copy(args_copy, args);
    const int required_size = std::vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);

    if (required_size < 0) {
        return;
    }

    std::vector<char> message(static_cast<size_t>(required_size) + 1U, '\0');
    (void)std::vsnprintf(message.data(), message.size(), format, args);

    FILE* out = (level >= LogLevel::Error) ? stderr : stdout;
    (void)std::fputs(prefix, out);
    (void)std::fputs(message.data(), out);
    (void)std::fputc('\n', out);

    if (callback != nullptr) {
        callback(level, tag, message.data());
    }
}
