/**
 * @file ConsoleLogSink.hpp
 * @ingroup logging
 * @brief Standard console output log sink with ANSI color support.
 */

#pragma once

#include <iostream>
#include "corium/logging/LogEvent.hpp"

namespace corium::logging::sinks {

/// @brief Standard console output log sink with optional ANSI color codes.
class ConsoleLogSink {
public:
    explicit ConsoleLogSink(bool useColors = true) noexcept
        : _useColors(useColors)
    {}

    /// @brief Write log event to std::cout or std::cerr.
    template <std::size_t N>
    void write(const LogEventT<N>& event) const
    {
        std::ostream& os = (event.level >= LogLevel::Error) ? std::cerr : std::cout;

        if (_useColors) {
            os << logLevelToColor(event.level)
               << "[" << logLevelToString(event.level) << "]"
               << LOG_COLOR_RESET
               << " [" << event.category << "] "
               << event.view() << "\n";
        } else {
            os << "[" << logLevelToString(event.level) << "]"
               << " [" << event.category << "] "
               << event.view() << "\n";
        }
        os << std::flush;
    }

    /// @brief Enable or disable ANSI color formatting.
    void setColorsEnabled(bool enable) noexcept
    {
        _useColors = enable;
    }

    [[nodiscard]] bool colorsEnabled() const noexcept
    {
        return _useColors;
    }

private:
    bool _useColors = true;
};

} // namespace corium::logging::sinks
