#include "app_log.h"

#include <algorithm>
#include <atomic>
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
constexpr size_t MESSAGE_BUFFER_SIZE = 1024;
constexpr size_t LOG_QUEUE_SIZE = 4096;

LogLevel g_log_level = LogLevel::Info;
bool g_log_initialized = false;
LogCallback g_log_callback = nullptr;
std::mutex g_log_mutex;

struct LogEntry {
    std::atomic<bool> ready{false};
    LogLevel level;
    char tag[32];
    char message[MESSAGE_BUFFER_SIZE];
    char time_buf[TIME_BUFFER_SIZE];
    int32_t pid;
    unsigned long long tid_hash;
    long long millis;
};

LogEntry g_log_queue[LOG_QUEUE_SIZE];
std::atomic<size_t> g_write_idx{0};
std::atomic<size_t> g_read_idx{0};

std::thread g_logger_thread;
std::atomic<bool> g_logger_running{false};

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
#ifdef _WIN32
    localtime_s(out_tm, &seconds);
#else
    localtime_r(&seconds, out_tm);
#endif
}

void logger_thread_func() {
    while (g_logger_running.load(std::memory_order_relaxed) || g_read_idx.load(std::memory_order_relaxed) < g_write_idx.load(std::memory_order_relaxed)) {
        size_t read_idx = g_read_idx.load(std::memory_order_relaxed);
        size_t write_idx = g_write_idx.load(std::memory_order_acquire);

        if (read_idx < write_idx) {
            LogEntry& entry = g_log_queue[read_idx % LOG_QUEUE_SIZE];

            while (!entry.ready.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            FILE* out = (entry.level >= LogLevel::Error) ? stderr : stdout;

            char prefix[PREFIX_BUFFER_SIZE] = {};
            (void)std::snprintf(prefix, sizeof(prefix), "%s,%03lld [%d:%llu] - %s - %-8s - ", entry.time_buf, entry.millis, entry.pid, entry.tid_hash,
                                entry.tag[0] ? entry.tag : "app", level_to_string(entry.level));

            (void)std::fputs(prefix, out);
            (void)std::fputs(entry.message, out);
            (void)std::fputc('\n', out);
            (void)std::fflush(out);

            if (g_log_callback != nullptr) {
                g_log_callback(entry.level, entry.tag, entry.message);
            }

            entry.ready.store(false, std::memory_order_release);
            g_read_idx.fetch_add(1, std::memory_order_relaxed);
        } else {
            if (!g_logger_running.load(std::memory_order_relaxed)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

} // namespace

void log_init() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_logger_running.exchange(true)) {
        g_logger_thread = std::thread(logger_thread_func);
    }
}

void log_shutdown() {
    g_logger_running.store(false);
    if (g_logger_thread.joinable()) {
        g_logger_thread.join();
    }
}

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
    if (level < g_log_level) {
        return;
    }

    if (!g_log_initialized) {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        ensure_log_initialized_locked();
        if (level < g_log_level) {
            return;
        }
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

    if (!g_logger_running.load(std::memory_order_relaxed)) {
        // Fallback to synchronous if logger thread is not running
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

        char* message = static_cast<char*>(__builtin_alloca(required_size + 1));
        (void)std::vsnprintf(message, required_size + 1, format, args);

        FILE* out = (level >= LogLevel::Error) ? stderr : stdout;
        (void)std::fputs(prefix, out);
        (void)std::fputs(message, out);
        (void)std::fputc('\n', out);
        (void)std::fflush(out);

        if (g_log_callback != nullptr) {
            g_log_callback(level, tag, message);
        }
        return;
    }

    size_t write_idx = g_write_idx.fetch_add(1, std::memory_order_relaxed);

    // If the queue is full, spin until space is available
    while (write_idx - g_read_idx.load(std::memory_order_acquire) >= LOG_QUEUE_SIZE) {
        std::this_thread::yield();
    }

    LogEntry& entry = g_log_queue[write_idx % LOG_QUEUE_SIZE];
    entry.level = level;
    if (tag) {
        strncpy(entry.tag, tag, sizeof(entry.tag) - 1);
        entry.tag[sizeof(entry.tag) - 1] = '\0';
    } else {
        entry.tag[0] = '\0';
    }

    std::vsnprintf(entry.message, sizeof(entry.message), format, args);
    std::memcpy(entry.time_buf, time_buf, sizeof(entry.time_buf));
    entry.pid = pid;
    entry.tid_hash = tid_hash;
    entry.millis = static_cast<long long>(millis);

    entry.ready.store(true, std::memory_order_release);
}

const char* log_format(const char* format, ...) {
    constexpr size_t FORMAT_RING_BUFFER_SIZE = 8;
    constexpr size_t FORMAT_STRING_SIZE = 1024;

    struct RingBuffer {
        char buffers[FORMAT_RING_BUFFER_SIZE][FORMAT_STRING_SIZE];
        size_t index = 0;
    };

    thread_local RingBuffer tls_ring_buffer;

    char* current_buffer = tls_ring_buffer.buffers[tls_ring_buffer.index];
    tls_ring_buffer.index = (tls_ring_buffer.index + 1) % FORMAT_RING_BUFFER_SIZE;

    va_list args;
    va_start(args, format);
    (void)std::vsnprintf(current_buffer, FORMAT_STRING_SIZE, format, args);
    va_end(args);

    return current_buffer;
}
