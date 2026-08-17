#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "corium/Events.hpp"
#include "corium/policies/QueuePolicies.hpp"
#include "corium/timers/ClockPolicies.hpp"

namespace corium {

using TimerId = uint32_t;
constexpr TimerId INVALID_TIMER_ID = 0;

/// @brief Zero-heap Timer Scheduler storing timers in a fixed-capacity static array.
/// Supports single-shot delayed events and recurring periodic events with compile-time clock policy.
/// @tparam EventVariant Supported event variant list type.
/// @tparam MaxTimers Maximum number of concurrent active timers (static array capacity).
/// @tparam ClockPolicy Policy governing time acquisition and arithmetic (ChronoClockPolicy default).
template <
    typename EventVariant = DefaultEvents,
    size_t MaxTimers = 64,
    typename ClockPolicy = ChronoClockPolicy
>
class TimerScheduler {
public:
    using Clock = ClockPolicy;
    using time_point = typename ClockPolicy::time_point;
    using duration = typename ClockPolicy::duration;

    TimerScheduler() = default;

    /// @brief Schedule a single-shot delayed event with std::chrono duration.
    template <typename Rep, typename Period>
    TimerId scheduleDelayed(EventVariant event, const std::chrono::duration<Rep, Period>& delay, EventPriority priority = EventPriority::Normal)
    {
        time_point now = ClockPolicy::now();
        time_point expiry = ClockPolicy::add(now, delay);
        duration zeroInterval{};
        return allocateTimer(std::move(event), expiry, zeroInterval, priority, false);
    }

    /// @brief Schedule a single-shot delayed event with native clock duration.
    TimerId scheduleDelayed(EventVariant event, duration delay, EventPriority priority = EventPriority::Normal)
        requires (!std::is_same_v<duration, std::chrono::microseconds> && !std::is_same_v<duration, std::chrono::milliseconds>)
    {
        time_point now = ClockPolicy::now();
        time_point expiry = ClockPolicy::add(now, delay);
        duration zeroInterval{};
        return allocateTimer(std::move(event), expiry, zeroInterval, priority, false);
    }

    /// @brief Schedule a recurring periodic event with std::chrono duration.
    template <typename Rep, typename Period>
    TimerId schedulePeriodic(EventVariant event, const std::chrono::duration<Rep, Period>& interval, EventPriority priority = EventPriority::Normal)
    {
        time_point now = ClockPolicy::now();
        time_point expiry = ClockPolicy::add(now, interval);
        if constexpr (std::is_same_v<duration, std::chrono::microseconds>) {
            return allocateTimer(std::move(event), expiry, std::chrono::duration_cast<std::chrono::microseconds>(interval), priority, true);
        } else if constexpr (std::is_same_v<duration, std::chrono::milliseconds>) {
            return allocateTimer(std::move(event), expiry, std::chrono::duration_cast<std::chrono::milliseconds>(interval), priority, true);
        } else {
            duration nativeInterval = static_cast<duration>(std::chrono::duration_cast<std::chrono::microseconds>(interval).count());
            return allocateTimer(std::move(event), expiry, nativeInterval, priority, true);
        }
    }

    /// @brief Schedule a recurring periodic event with native clock duration.
    TimerId schedulePeriodic(EventVariant event, duration interval, EventPriority priority = EventPriority::Normal)
        requires (!std::is_same_v<duration, std::chrono::microseconds> && !std::is_same_v<duration, std::chrono::milliseconds>)
    {
        time_point now = ClockPolicy::now();
        time_point expiry = ClockPolicy::add(now, interval);
        return allocateTimer(std::move(event), expiry, interval, priority, true);
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

    /// @brief Process all due timers and post their events into target event bus or sink.
    /// @tparam TargetEventSink Target event sink type (e.g. BasicEventBus or EventSink).
    /// @param sink Target sink receiving due timer events.
    /// @param now Current time point (defaults to ClockPolicy::now()).
    /// @return Number of events posted during this check.
    template <typename EventSink>
    std::size_t processDueTimers(EventSink& sink, time_point now = ClockPolicy::now())
    {
        if (_activeCount == 0) {
            return 0;
        }

        std::size_t posted = 0;
        for (auto& entry : _timers) {
            if (!entry.active) {
                continue;
            }

            if (ClockPolicy::isDue(now, entry.expiryTime)) {
                sink.post(entry.event, entry.priority);
                posted++;

                if (entry.isPeriodic) {
                    entry.expiryTime = ClockPolicy::add(now, entry.interval);
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
    TimerId allocateTimer(EventVariant event, time_point expiryTime, duration interval, EventPriority priority, bool isPeriodic)
    {
        for (auto& entry : _timers) {
            if (!entry.active) {
                entry.id = _nextId++;
                if (_nextId == INVALID_TIMER_ID) {
                    _nextId = 1;
                }
                entry.event = std::move(event);
                entry.expiryTime = expiryTime;
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

    struct TimerEntry {
        TimerId id = INVALID_TIMER_ID;
        EventVariant event{};
        time_point expiryTime{};
        duration interval{};
        EventPriority priority = EventPriority::Normal;
        bool isPeriodic = false;
        bool active = false;
    };

    std::array<TimerEntry, MaxTimers> _timers{};
    size_t _activeCount = 0;
    TimerId _nextId = 1;
};

} // namespace corium
