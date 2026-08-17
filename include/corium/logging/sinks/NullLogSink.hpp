#pragma once

#include "corium/logging/LogEvent.hpp"

namespace corium::logging::sinks {

/// @brief No-op log sink for zero-cost logging in benchmarks or production headless mode.
class NullLogSink {
public:
    NullLogSink() = default;

    template <std::size_t N>
    void write(const LogEventT<N>& event) const noexcept
    {
        (void)event;
    }
};

} // namespace corium::logging::sinks
