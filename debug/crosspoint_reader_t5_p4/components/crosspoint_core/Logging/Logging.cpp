#include "Logging.h"

#include <Arduino.h>
#include <esp_log.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr size_t kMaxEntryLen = 256;
constexpr size_t kMaxLogLines = 16;

std::string s_log_lines[kMaxLogLines];
size_t s_log_head = 0;

esp_log_level_t level_to_esp(const char* level)
{
    if (std::strcmp(level, "ERR") == 0) {
        return ESP_LOG_ERROR;
    }
    if (std::strcmp(level, "DBG") == 0) {
        return ESP_LOG_DEBUG;
    }
    return ESP_LOG_INFO;
}

void add_to_ring(const char* line)
{
    s_log_lines[s_log_head] = line;
    s_log_head = (s_log_head + 1) % kMaxLogLines;
}

}  // namespace

void logPrintf(const char* level, const char* origin, const char* format, ...)
{
    char message[kMaxEntryLen] = {};

    const int prefix_len = std::snprintf(message, sizeof(message), "[%lu] [%s] [%s] ",
                                         static_cast<unsigned long>(millis()), level, origin);
    if (prefix_len < 0 || static_cast<size_t>(prefix_len) >= sizeof(message)) {
        return;
    }

    va_list args;
    va_start(args, format);
    std::vsnprintf(message + prefix_len, sizeof(message) - static_cast<size_t>(prefix_len), format, args);
    va_end(args);

    esp_log_write(level_to_esp(level), origin, "%s", message);
    add_to_ring(message);
}

std::string getLastLogs()
{
    std::string output;
    for (size_t i = 0; i < kMaxLogLines; ++i) {
        const size_t index = (s_log_head + i) % kMaxLogLines;
        if (!s_log_lines[index].empty()) {
            output += s_log_lines[index];
        }
    }
    return output;
}

void clearLastLogs()
{
    for (auto& line : s_log_lines) {
        line.clear();
    }
    s_log_head = 0;
}

bool sanitizeLogHead()
{
    return false;
}
