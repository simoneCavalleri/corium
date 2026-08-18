/**
 * @file JsonLogSink.hpp
 * @ingroup logging
 * @brief Structured JSON Lines (NDJSON) output log sink for observability.
 */

#pragma once

#include <ostream>
#include <string_view>
#include "corium/logging/LogEvent.hpp"

namespace corium::logging::sinks {

/// @ingroup logging
/// @brief Structured JSON Lines (NDJSON) output log sink.
/// Formats LogEvent records into JSON objects for structured telemetry and ingest (ELK, Datadog, Grafana Loki).
class JsonLogSink {
public:
    explicit JsonLogSink(std::ostream& outputStream) noexcept
        : _os(&outputStream)
    {}

    /// @brief Serialize log event as a single-line JSON string.
    template <std::size_t N>
    void write(const LogEventT<N>& event) const
    {
        if (!_os) return;

        *_os << "{\"timestamp_ns\":" << event.timestampNs
             << ",\"level\":\"" << logLevelToString(event.level) << "\""
             << ",\"category\":\"" << event.category << "\""
             << ",\"message\":\"";

        // Escape JSON string characters
        std::string_view msg = event.view();
        for (char c : msg) {
            switch (c) {
                case '"':  *_os << "\\\""; break;
                case '\\': *_os << "\\\\"; break;
                case '\b': *_os << "\\b";  break;
                case '\f': *_os << "\\f";  break;
                case '\n': *_os << "\\n";  break;
                case '\r': *_os << "\\r";  break;
                case '\t': *_os << "\\t";  break;
                default:   *_os << c;       break;
            }
        }

        *_os << "\"}\n" << std::flush;
    }

private:
    std::ostream* _os{nullptr};
};

} // namespace corium::logging::sinks
