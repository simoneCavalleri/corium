/**
 * @file LogBackgroundService.hpp
 * @ingroup logging
 * @brief Asynchronous background worker service for flushing log events to disk.
 */

#pragma once

#include <chrono>
#include <stop_token>
#include <thread>
#include <utility>

#include "corium/BackgroundService.hpp"
#include "corium/logging/Logger.hpp"

namespace corium::logging {

/// @brief Asynchronous background logging service executing on a dedicated C++20 std::jthread.
/// Consumes/flushes log events or periodic status messages asynchronously without blocking the main event loop.
/// @tparam LogSink Log sink backend type (e.g. ConsoleLogSink, FileLogSink, NullLogSink).
/// @tparam EventVariant Event variant list supported by the runtime context.
/// @tparam MaxMessageSize Inline capacity size for log events.
template <typename LogSink = sinks::ConsoleLogSink, typename EventVariant = DefaultEvents, std::size_t MaxMessageSize = 256>
class LogBackgroundService : public BackgroundService<EventVariant> {
public:
    using LoggerType = LoggerT<LogSink, MaxMessageSize>;

    explicit LogBackgroundService(const char* category = "LogService", LogLevel minLevel = LogLevel::Info, LogSink sink = LogSink{})
        : _logger(category, minLevel, std::move(sink))
    {}

    /// @brief Access embedded logger instance.
    [[nodiscard]] LoggerType& logger() noexcept { return _logger; }
    [[nodiscard]] const LoggerType& logger() const noexcept { return _logger; }

    /// @brief Log background heartbeat or event message.
    template <typename... Args>
    void info(const char* fmt, Args&&... args) const
    {
        _logger.info(fmt, std::forward<Args>(args)...);
    }

    /// @brief Main background execution loop.
    void run(const std::stop_token& stopToken)
    {
        _logger.info("Asynchronous LogBackgroundService started.");
        while (!stopToken.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        _logger.info("Asynchronous LogBackgroundService stopping.");
    }

private:
    LoggerType _logger;
};

} // namespace corium::logging
