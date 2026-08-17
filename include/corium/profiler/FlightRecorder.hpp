#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ostream>

namespace corium::profiler {

/// @brief Timestamp clock used by the profiler (steady clock in nanoseconds).
using ProfilerClock = std::chrono::steady_clock;

/// @brief Represents a single traced event record in the circular flight recorder.
struct alignas(32) FlightRecord {
    std::size_t eventTypeId{0};
    const char* eventName{nullptr};
    uint64_t postTimestampNs{0};
    uint64_t dispatchTimestampNs{0};
    uint64_t finishTimestampNs{0};
    uint8_t priority{0};

    /// @brief Calculate latency spent waiting in queue before dispatch in microseconds.
    [[nodiscard]] double queueLatencyUs() const noexcept
    {
        if (dispatchTimestampNs < postTimestampNs) return 0.0;
        return static_cast<double>(dispatchTimestampNs - postTimestampNs) / 1000.0;
    }

    /// @brief Calculate duration of handler execution in microseconds.
    [[nodiscard]] double executionDurationUs() const noexcept
    {
        if (finishTimestampNs < dispatchTimestampNs) return 0.0;
        return static_cast<double>(finishTimestampNs - dispatchTimestampNs) / 1000.0;
    }

    /// @brief Calculate total turnaround time from post to finish in microseconds.
    [[nodiscard]] double totalDurationUs() const noexcept
    {
        if (finishTimestampNs < postTimestampNs) return 0.0;
        return static_cast<double>(finishTimestampNs - postTimestampNs) / 1000.0;
    }
};

/// @ingroup profiler
/// @brief Zero-heap circular flight recorder storing the last N event telemetry records.
/// Thread-safe for multiple producers and concurrent reader/dumping.
/// @tparam Capacity Number of flight records (power of 2).
template <std::size_t Capacity = 256>
class FlightRecorder {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2.");
    static constexpr std::size_t Mask = Capacity - 1;

public:
    FlightRecorder() = default;

    /// @brief Record a completed event execution into the circular buffer.
    void record(
        std::size_t eventTypeId,
        const char* eventName,
        uint64_t postTimeNs,
        uint64_t dispatchTimeNs,
        uint64_t finishTimeNs,
        uint8_t priority
    ) noexcept
    {
        const std::size_t idx = _writeIndex.fetch_add(1, std::memory_order_relaxed);
        FlightRecord& entry = _records[idx & Mask];
        entry.eventTypeId = eventTypeId;
        entry.eventName = eventName ? eventName : "UnknownEvent";
        entry.postTimestampNs = postTimeNs;
        entry.dispatchTimestampNs = dispatchTimeNs;
        entry.finishTimestampNs = finishTimeNs;
        entry.priority = priority;
    }

    /// @brief Get total number of recorded events since creation.
    [[nodiscard]] uint64_t totalRecorded() const noexcept
    {
        return _writeIndex.load(std::memory_order_relaxed);
    }

    /// @brief Get buffer capacity.
    [[nodiscard]] constexpr std::size_t capacity() const noexcept
    {
        return Capacity;
    }

    /// @brief Read a record by logical historical index (0 is oldest available in current window).
    [[nodiscard]] FlightRecord recordAt(std::size_t index) const noexcept
    {
        return _records[index & Mask];
    }

    /// @brief Iterate through available historical records in chronological order.
    template <typename Callback>
    void forEach(Callback&& cb) const
    {
        const uint64_t current = _writeIndex.load(std::memory_order_acquire);
        const std::size_t count = current < Capacity ? static_cast<std::size_t>(current) : Capacity;
        const uint64_t start = current < Capacity ? 0 : current - Capacity;

        for (std::size_t i = 0; i < count; ++i) {
            cb(_records[(start + i) & Mask]);
        }
    }

    /// @brief Export recorded flight logs in Chrome Tracing JSON format (supported by chrome://tracing and Perfetto UI).
    void exportChromeTracingJson(std::ostream& os) const
    {
        os << "[\n";
        bool first = true;

        forEach([&](const FlightRecord& rec) {
            if (!first) os << ",\n";
            first = false;

            // 1. Queue wait event (phase "X" complete event)
            const double postUs = static_cast<double>(rec.postTimestampNs) / 1000.0;
            const double queueDurationUs = rec.queueLatencyUs();
            const double dispatchUs = static_cast<double>(rec.dispatchTimestampNs) / 1000.0;
            const double execDurationUs = rec.executionDurationUs();

            os << "  {\"name\": \"QueueWait[" << (rec.eventName ? rec.eventName : "Event")
               << "]\", \"cat\": \"corium\", \"ph\": \"X\", \"ts\": " << postUs
               << ", \"dur\": " << queueDurationUs << ", \"pid\": 1, \"tid\": 1, \"args\": {\"prio\": "
               << static_cast<int>(rec.priority) << "}},\n";

            // 2. Dispatch / Handler execution event
            os << "  {\"name\": \"Dispatch[" << (rec.eventName ? rec.eventName : "Event")
               << "]\", \"cat\": \"corium\", \"ph\": \"X\", \"ts\": " << dispatchUs
               << ", \"dur\": " << execDurationUs << ", \"pid\": 1, \"tid\": 1, \"args\": {\"type_id\": "
               << rec.eventTypeId << "}}";
        });

        os << "\n]\n";
    }

private:
    std::atomic<uint64_t> _writeIndex{0};
    std::array<FlightRecord, Capacity> _records{};
};

} // namespace corium::profiler
