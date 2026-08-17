#pragma once

#include <cstdio>
#include <utility>

#include "corium/logging/LogEvent.hpp"
#include "corium/logging/sinks/ConsoleLogSink.hpp"
#include "corium/logging/sinks/FileLogSink.hpp"
#include "corium/logging/sinks/NullLogSink.hpp"

namespace corium::logging {

/// @ingroup logging
/// @brief Static compile-time logger wrapper managing severity level filtering and zero-heap string formatting.
/// @tparam LogSink Log sink backend handling actual log entry output (e.g. ConsoleLogSink, FileLogSink, NullLogSink).
/// @tparam MaxMessageSize Maximum character length for formatted log messages.
template <typename LogSink = sinks::ConsoleLogSink, std::size_t MaxMessageSize = 256>
class LoggerT {
public:
    using EventType = LogEventT<MaxMessageSize>;

    explicit LoggerT(const char* category = "App", LogLevel minLevel = LogLevel::Info, LogSink sink = LogSink{})
        : _category(category ? category : "App"), _minLevel(minLevel), _sink(std::move(sink))
    {}

    /// @brief Set minimum log severity level.
    void setMinLevel(LogLevel level) noexcept
    {
        _minLevel = level;
    }

    /// @brief Get minimum log severity level.
    [[nodiscard]] LogLevel minLevel() const noexcept
    {
        return _minLevel;
    }

    /// @brief Set category string tag.
    void setCategory(const char* category) noexcept
    {
        _category = category ? category : "App";
    }

    /// @brief Get category tag.
    [[nodiscard]] const char* category() const noexcept
    {
        return _category;
    }

    /// @brief Access underlying log sink reference.
    [[nodiscard]] LogSink& sink() noexcept { return _sink; }
    [[nodiscard]] const LogSink& sink() const noexcept { return _sink; }

    /// @brief Log formatted message directly to sink if severity level meets minimum threshold.
    template <typename... Args>
    void log(LogLevel level, const char* fmt, Args&&... args) const
    {
        if (level < _minLevel || _minLevel == LogLevel::Off) {
            return;
        }

        EventType event{};
        event.level = level;
        event.category = _category;

        if constexpr (sizeof...(Args) == 0) {
            event.setMessage(fmt);
        } else {
            int written = std::snprintf(event.message.data(), MaxMessageSize, fmt, std::forward<Args>(args)...);
            if (written > 0) {
                event.length = static_cast<std::size_t>(written) < MaxMessageSize ? static_cast<std::size_t>(written) : (MaxMessageSize - 1);
            }
        }

        _sink.write(event);
    }

    /// @brief Post formatted LogEvent to a target Corium EventSink (lock-free MPSC event bus).
    template <typename TargetEventSink, typename... Args>
    void logToSink(TargetEventSink&& targetSink, LogLevel level, const char* fmt, Args&&... args) const
    {
        if (level < _minLevel || _minLevel == LogLevel::Off) {
            return;
        }

        EventType event{};
        event.level = level;
        event.category = _category;

        if constexpr (sizeof...(Args) == 0) {
            event.setMessage(fmt);
        } else {
            int written = std::snprintf(event.message.data(), MaxMessageSize, fmt, std::forward<Args>(args)...);
            if (written > 0) {
                event.length = static_cast<std::size_t>(written) < MaxMessageSize ? static_cast<std::size_t>(written) : (MaxMessageSize - 1);
            }
        }

        targetSink.post(std::move(event));
    }

    template <typename... Args>
    void trace(const char* fmt, Args&&... args) const { log(LogLevel::Trace, fmt, std::forward<Args>(args)...); }

    template <typename... Args>
    void debug(const char* fmt, Args&&... args) const { log(LogLevel::Debug, fmt, std::forward<Args>(args)...); }

    template <typename... Args>
    void info(const char* fmt, Args&&... args) const { log(LogLevel::Info, fmt, std::forward<Args>(args)...); }

    template <typename... Args>
    void warn(const char* fmt, Args&&... args) const { log(LogLevel::Warn, fmt, std::forward<Args>(args)...); }

    template <typename... Args>
    void error(const char* fmt, Args&&... args) const { log(LogLevel::Error, fmt, std::forward<Args>(args)...); }

    template <typename... Args>
    void critical(const char* fmt, Args&&... args) const { log(LogLevel::Critical, fmt, std::forward<Args>(args)...); }

private:
    const char* _category = "App";
    LogLevel _minLevel = LogLevel::Info;
    LogSink _sink{};
};

/// @brief Default Console Logger alias.
using ConsoleLogger = LoggerT<sinks::ConsoleLogSink>;

/// @brief Default File Logger alias.
using FileLogger = LoggerT<sinks::FileLogSink>;

/// @brief Default Null Logger alias.
using NullLogger = LoggerT<sinks::NullLogSink>;

} // namespace corium::logging
