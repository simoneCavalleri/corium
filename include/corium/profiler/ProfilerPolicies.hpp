#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ostream>

#include "corium/internal/MpscRingBuffer.hpp"
#include "corium/profiler/FlightRecorder.hpp"

namespace corium::profiler {

/// @brief Default Profiler Policy: Zero-overhead, completely compiled out by inline empty functions.
struct NullProfiler {
    constexpr NullProfiler() noexcept = default;

    template <typename EventVariant>
    void onEventPosted(const EventVariant&, uint8_t) noexcept {}

    template <typename EventVariant>
    void onEventDispatched(
        const EventVariant&,
        uint8_t,
        uint64_t /*postTimeNs*/,
        uint64_t /*dispatchTimeNs*/,
        uint64_t /*finishTimeNs*/
    ) noexcept {}

    [[nodiscard]] static constexpr uint64_t nowNs() noexcept { return 0; }

    /// @brief No-op: NullProfiler does not track post timestamps.
    void recordPostTime(uint64_t) noexcept {}

    /// @brief No-op: always returns 0 (NullProfiler has no timestamps).
    [[nodiscard]] uint64_t takePostTime() noexcept { return 0; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Internal: parallel timestamp ring buffer for tracking per-event post times.
//
// Uses a separate MpscRingBuffer<uint64_t, Capacity> that is pushed in lockstep
// with the main event queue. Because the event bus is MPSC and dispatch is
// single-consumer (same thread that calls pump()), timestamps arrive and are
// consumed in FIFO order, so the i-th timestamp always belongs to the i-th event.
//
// Zero-overhead when used with NullProfiler (the entire type is not instantiated).
// ─────────────────────────────────────────────────────────────────────────────
template <std::size_t Capacity = 1024>
class PostTimestampQueue {
public:
    /// @brief Record the post timestamp for an event being pushed into the event queue.
    void recordPostTime(uint64_t postNs) noexcept
    {
        // Best-effort: if the timestamp queue is full (e.g. profiler not being read),
        // drop the timestamp rather than blocking or corrupting the event queue.
        _timestamps.tryPush(postNs);
    }

    /// @brief Pop and return the oldest recorded post timestamp (called at dispatch time).
    /// @return The post timestamp, or 0 if the queue is empty (should not happen in steady state).
    [[nodiscard]] uint64_t takePostTime() noexcept
    {
        uint64_t ts = 0;
        _timestamps.tryPop(ts);
        return ts;
    }

private:
    MpscRingBuffer<uint64_t, Capacity> _timestamps;
};

// ─────────────────────────────────────────────────────────────────────────────

/// @ingroup profiler
/// @brief Real-time event latency and performance statistics tracker.
/// Zero dynamic memory allocation. Tracks min, max, total queue latency and handler duration.
/// @tparam QueueCapacity Capacity of the parallel post-timestamp ring buffer (must match
///         the event queue capacity for accurate per-event latency tracking).
template <std::size_t QueueCapacity = 1024>
class LatencyTracker {
public:
    LatencyTracker() noexcept = default;

    [[nodiscard]] static uint64_t nowNs() noexcept
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                ProfilerClock::now().time_since_epoch()
            ).count()
        );
    }

    template <typename EventVariant>
    void onEventPosted(const EventVariant&, uint8_t) noexcept
    {
        _totalPosted.fetch_add(1, std::memory_order_relaxed);
    }

    /// @brief Record the wall-clock post timestamp for the event being pushed now.
    /// Called by EventBus::post() immediately after the event enters the queue.
    void recordPostTime(uint64_t postNs) noexcept
    {
        _postTimestamps.recordPostTime(postNs);
    }

    /// @brief Pop and return the oldest post timestamp (called at dispatch time by EventBus).
    [[nodiscard]] uint64_t takePostTime() noexcept
    {
        return _postTimestamps.takePostTime();
    }

    template <typename EventVariant>
    void onEventDispatched(
        const EventVariant&,
        uint8_t,
        uint64_t postTimeNs,
        uint64_t dispatchTimeNs,
        uint64_t finishTimeNs
    ) noexcept
    {
        const uint64_t queueLatencyNs = (dispatchTimeNs > postTimeNs) ? (dispatchTimeNs - postTimeNs) : 0;
        const uint64_t execDurationNs = (finishTimeNs > dispatchTimeNs) ? (finishTimeNs - dispatchTimeNs) : 0;

        _totalDispatched.fetch_add(1, std::memory_order_relaxed);
        _totalQueueLatencyNs.fetch_add(queueLatencyNs, std::memory_order_relaxed);
        _totalExecDurationNs.fetch_add(execDurationNs, std::memory_order_relaxed);

        // Update Max Latency
        uint64_t currentMaxLat = _maxQueueLatencyNs.load(std::memory_order_relaxed);
        while (queueLatencyNs > currentMaxLat &&
               !_maxQueueLatencyNs.compare_exchange_weak(currentMaxLat, queueLatencyNs, std::memory_order_relaxed)) {}

        // Update Min Latency
        uint64_t currentMinLat = _minQueueLatencyNs.load(std::memory_order_relaxed);
        while (queueLatencyNs < currentMinLat &&
               !_minQueueLatencyNs.compare_exchange_weak(currentMinLat, queueLatencyNs, std::memory_order_relaxed)) {}

        // Update Max Execution Duration
        uint64_t currentMaxExec = _maxExecDurationNs.load(std::memory_order_relaxed);
        while (execDurationNs > currentMaxExec &&
               !_maxExecDurationNs.compare_exchange_weak(currentMaxExec, execDurationNs, std::memory_order_relaxed)) {}
    }

    /// @brief Total count of posted events.
    [[nodiscard]] uint64_t totalPosted() const noexcept
    {
        return _totalPosted.load(std::memory_order_relaxed);
    }

    /// @brief Total count of dispatched events.
    [[nodiscard]] uint64_t totalDispatched() const noexcept
    {
        return _totalDispatched.load(std::memory_order_relaxed);
    }

    /// @brief Minimum queue latency in microseconds.
    [[nodiscard]] double minQueueLatencyUs() const noexcept
    {
        const uint64_t val = _minQueueLatencyNs.load(std::memory_order_relaxed);
        return val == UINT64_MAX ? 0.0 : static_cast<double>(val) / 1000.0;
    }

    /// @brief Maximum queue latency in microseconds.
    [[nodiscard]] double maxQueueLatencyUs() const noexcept
    {
        return static_cast<double>(_maxQueueLatencyNs.load(std::memory_order_relaxed)) / 1000.0;
    }

    /// @brief Average queue latency in microseconds.
    [[nodiscard]] double averageQueueLatencyUs() const noexcept
    {
        const uint64_t count = _totalDispatched.load(std::memory_order_relaxed);
        if (count == 0) return 0.0;
        return static_cast<double>(_totalQueueLatencyNs.load(std::memory_order_relaxed)) / (static_cast<double>(count) * 1000.0);
    }

    /// @brief Maximum handler execution duration in microseconds.
    [[nodiscard]] double maxExecutionDurationUs() const noexcept
    {
        return static_cast<double>(_maxExecDurationNs.load(std::memory_order_relaxed)) / 1000.0;
    }

    /// @brief Average handler execution duration in microseconds.
    [[nodiscard]] double averageExecutionDurationUs() const noexcept
    {
        const uint64_t count = _totalDispatched.load(std::memory_order_relaxed);
        if (count == 0) return 0.0;
        return static_cast<double>(_totalExecDurationNs.load(std::memory_order_relaxed)) / (static_cast<double>(count) * 1000.0);
    }

    /// @brief Reset all accumulated statistics.
    void resetStats() noexcept
    {
        _totalPosted.store(0, std::memory_order_relaxed);
        _totalDispatched.store(0, std::memory_order_relaxed);
        _totalQueueLatencyNs.store(0, std::memory_order_relaxed);
        _totalExecDurationNs.store(0, std::memory_order_relaxed);
        _maxQueueLatencyNs.store(0, std::memory_order_relaxed);
        _minQueueLatencyNs.store(UINT64_MAX, std::memory_order_relaxed);
        _maxExecDurationNs.store(0, std::memory_order_relaxed);
    }

private:
    PostTimestampQueue<QueueCapacity> _postTimestamps;

    std::atomic<uint64_t> _totalPosted{0};
    std::atomic<uint64_t> _totalDispatched{0};
    std::atomic<uint64_t> _totalQueueLatencyNs{0};
    std::atomic<uint64_t> _totalExecDurationNs{0};
    std::atomic<uint64_t> _maxQueueLatencyNs{0};
    std::atomic<uint64_t> _minQueueLatencyNs{UINT64_MAX};
    std::atomic<uint64_t> _maxExecDurationNs{0};
};

/// @brief Combined Flight Recorder and Latency Tracker Profiler.
/// Records historical event traces into a circular in-memory buffer with zero heap allocations.
/// @tparam BufferCapacity Capacity of circular flight recorder (power of 2, e.g. 128, 256, 1024).
/// @tparam QueueCapacity  Capacity of the parallel post-timestamp ring buffer (should match the
///         event queue capacity for accurate latency measurement; default 1024).
template <std::size_t BufferCapacity = 256, std::size_t QueueCapacity = 1024>
class FlightRecorderProfiler : public LatencyTracker<QueueCapacity> {
public:
    FlightRecorderProfiler() = default;

    template <typename EventVariant>
    void onEventDispatched(
        const EventVariant& event,
        uint8_t priority,
        uint64_t postTimeNs,
        uint64_t dispatchTimeNs,
        uint64_t finishTimeNs
    ) noexcept
    {
        LatencyTracker<QueueCapacity>::onEventDispatched(event, priority, postTimeNs, dispatchTimeNs, finishTimeNs);

        const std::size_t typeIndex = event.index();
        const char* name = "Event";

        _flightRecorder.record(typeIndex, name, postTimeNs, dispatchTimeNs, finishTimeNs, priority);
    }

    /// @brief Access reference to underlying circular flight recorder.
    [[nodiscard]] const FlightRecorder<BufferCapacity>& flightRecorder() const noexcept
    {
        return _flightRecorder;
    }

    /// @brief Export flight recorder traces to Chrome Tracing JSON.
    void exportChromeTracingJson(std::ostream& os) const
    {
        _flightRecorder.exportChromeTracingJson(os);
    }

private:
    FlightRecorder<BufferCapacity> _flightRecorder;
};

} // namespace corium::profiler
