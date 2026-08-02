#pragma once

#include <cstdint>

namespace corium::logging {

/// @brief Logging severity levels.
enum class LogLevel : uint8_t {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off
};

/// @brief Convert LogLevel enum to string representation.
constexpr const char* logLevelToString(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Trace:    return "TRACE";
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO";
        case LogLevel::Warn:     return "WARN";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRITICAL";
        case LogLevel::Off:      return "OFF";
    }
    return "UNKNOWN";
}

/// @brief Get ANSI color escape code for LogLevel.
constexpr const char* logLevelToColor(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Trace:    return "\033[36m"; // Cyan
        case LogLevel::Debug:    return "\033[34m"; // Blue
        case LogLevel::Info:     return "\033[32m"; // Green
        case LogLevel::Warn:     return "\033[33m"; // Yellow
        case LogLevel::Error:    return "\033[31m"; // Red
        case LogLevel::Critical: return "\033[35m"; // Magenta
        case LogLevel::Off:      return "\033[0m";  // Reset
    }
    return "\033[0m";
}

/// @brief ANSI reset color escape code.
constexpr const char* LOG_COLOR_RESET = "\033[0m";

} // namespace corium::logging
