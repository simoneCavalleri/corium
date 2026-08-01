#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "corium/Events.hpp"
#include "corium/policies/QueuePolicies.hpp"

namespace corium {

using TimerId = uint32_t;
constexpr TimerId INVALID_TIMER_ID = 0;

/// @brief Zero-heap Timer Scheduler storing timers in a fixed-capacity static array.
/// Supports single-shot delayed events and recurring periodic events.
/// @tparam EventVariant Supported event variant list type.
/// @tparam MaxTimers Maximum number of concurrent active timers (static array capacity).
template <typename EventVariant = DefaultEvents, size_t MaxTimers = 64>
class TimerScheduler {
public:
    struct TimerEntry {
        TimerId id = INVALID_TIMER_ID;
        EventVariant event{};
        std::chrono::steady_clock::time_point expiryTime{};
        std::chrono::microseconds interval{0};
        EventPriority priority = EventPriority::Normal;
        bool isPeriodic = false;
        bool active = false;
    };

    TimerScheduler() = default;

    /// @brief Schedule a single-shot delayed event.
    /// @param event Event instance to post upon expiry.
    /// @param delay Duration to wait before posting.
    /// @param priority Priority level of the event.
    /// @return Unique TimerId handle, or INVALID_TIMER_ID if timer capacity is full.
    TimerId scheduleDelayed(EventVariant event, std::chrono::microseconds delay, EventPriority priority = EventPriority::Normal)
    {
        return allocateTimer(std::move(event), delay, std::chrono::microseconds{0}, priority, false);
    }

    /// @brief Schedule a recurring periodic event.
    /// @param event Event instance to post periodically.
    /// @param interval Period between successive postings.
    /// @param priority Priority level of the event.
    /// @return Unique TimerId handle, or INVALID_TIMER_ID if timer capacity is full.
    TimerId schedulePeriodic(EventVariant event, std::chrono::microseconds interval, EventPriority priority = EventPriority::Normal)
    {
        return allocateTimer(std::move(event), interval, interval, priority, true);
    }

    /// @brief Cancel an active timer by its TimerId handle.
    /// @param id Handle of the timer to cancel.
    /// @return true if timer was found and cancelled; false if handle was invalid/inactive.
    bool cancelTimer(TimerId id) noexcept
    {
        if (id == INVALID_TIMER_ID) {
            return false;
        }

        for (auto& entry : _timers) {
            if (entry.active && entry.id == id) {
                entry.active = false;
                entry.id = INVALID_TIMER_ID;
                if (_activeCount > 0) {
                    _activeCount--;
                }
                return true;
            }
        }
        return false;
    }

    /// @brief Process all due timers against the current clock time and enqueue expired events into target eventSink.
    /// @tparam EventSink Target event sink type (e.g. BasicEventBus or IEventSinkT).
    /// @param sink Target sink receiving due events.
    /// @param now Current time point (defaults to std::chrono::steady_clock::now()).
    /// @return Number of events posted during this check.
    template <typename EventSink>
    std::size_t processDueTimers(EventSink& sink, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now())
    {
        if (_activeCount == 0) {
            return 0;
        }

        std::size_t posted = 0;
        for (auto& entry : _timers) {
            if (!entry.active) {
                continue;
            }

            if (now >= entry.expiryTime) {
                sink.post(entry.event, entry.priority);
                posted++;

                if (entry.isPeriodic) {
                    entry.expiryTime = now + entry.interval;
                } else {
                    entry.active = false;
                    entry.id = INVALID_TIMER_ID;
                    if (_activeCount > 0) {
                        _activeCount--;
                    }
                }
            }
        }
        return posted;
    }

    /// @brief Get current number of active timers.
    [[nodiscard]] size_t activeCount() const noexcept
    {
        return _activeCount;
    }

    /// @brief Get maximum timer capacity.
    [[nodiscard]] constexpr size_t capacity() const noexcept
    {
        return MaxTimers;
    }

private:
    TimerId allocateTimer(EventVariant event, std::chrono::microseconds delay, std::chrono::microseconds interval, EventPriority priority, bool isPeriodic)
    {
        auto now = std::chrono::steady_clock::now();

        for (auto& entry : _timers) {
            if (!entry.active) {
                entry.id = _nextId++;
                if (_nextId == INVALID_TIMER_ID) {
                    _nextId = 1;
                }
                entry.event = std::move(event);
                entry.expiryTime = now + delay;
                entry.interval = interval;
                entry.priority = priority;
                entry.isPeriodic = isPeriodic;
                entry.active = true;
                _activeCount++;
                return entry.id;
            }
        }

        return INVALID_TIMER_ID; // Capacity exceeded
    }

    std::array<TimerEntry, MaxTimers> _timers{};
    size_t _activeCount = 0;
    TimerId _nextId = 1;
};

} // namespace corium
