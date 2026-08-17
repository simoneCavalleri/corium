#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "corium/logging/LogLevel.hpp"

namespace corium::logging {

/// @brief Zero-heap log event carrying fixed-size inline message buffer across MPSC event bus.
/// @tparam MaxMessageLength Maximum character capacity for inline log message.
template <std::size_t MaxMessageLength = 256>
struct LogEventT {
    LogLevel level = LogLevel::Info;
    std::array<char, MaxMessageLength> message{};
    std::size_t length = 0;
    const char* category = "App";
    uint64_t timestampNs = 0;

    constexpr LogEventT() = default;

    /// @brief Create log event with inline formatted string.
    constexpr LogEventT(LogLevel lvl, std::string_view msg, const char* cat = "App", uint64_t ts = 0) noexcept
        : level(lvl), category(cat ? cat : "App"), timestampNs(ts)
    {
        setMessage(msg);
    }

    /// @brief Set inline message buffer safely up to MaxMessageLength - 1.
    constexpr void setMessage(std::string_view msg) noexcept
    {
        std::size_t copyLen = msg.length() < (MaxMessageLength - 1) ? msg.length() : (MaxMessageLength - 1);
        for (std::size_t i = 0; i < copyLen; ++i) {
            message[i] = msg[i];
        }
        message[copyLen] = '\0';
        length = copyLen;
    }

    /// @brief Access message as std::string_view.
    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view(message.data(), length);
    }
};

/// @brief Default LogEvent alias with 256-byte inline buffer.
using LogEvent = LogEventT<256>;

} // namespace corium::logging
