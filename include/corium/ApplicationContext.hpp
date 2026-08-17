#pragma once

#include <chrono>
#include "corium/EventBus.hpp"
#include "corium/EventSink.hpp"
#include "corium/policies/QueuePolicies.hpp"
#include "corium/policies/SignalPolicies.hpp"
#include "corium/timers/TimerScheduler.hpp"

namespace corium {

/// @brief Context object passed to Application providing event bus access, quit requests, and timer scheduling.
/// @tparam EventBusType Concrete event bus type used by the runtime.
template <typename EventBusType = EventBus>
class ApplicationContext {
public:
    using EventVariant = typename EventBusType::EventVariant;

    using ScheduleDelayedFn = TimerId (*)(void* ptr, EventVariant event, std::chrono::microseconds delay, EventPriority priority);
    using SchedulePeriodicFn = TimerId (*)(void* ptr, EventVariant event, std::chrono::microseconds interval, EventPriority priority);
    using CancelTimerFn = bool (*)(void* ptr, TimerId id);

    ApplicationContext() = default;

    ApplicationContext(EventBusType& events, StaticCallback quitCallback)
        : _events(&events), _quitCallback(quitCallback)
    {
    }

    template <typename Scheduler>
    void setTimerScheduler(Scheduler& scheduler) noexcept
    {
        _timerSchedulerPtr = &scheduler;
        _scheduleDelayedFn = [](void* ptr, EventVariant evt, std::chrono::microseconds delay, EventPriority prio) {
            return static_cast<Scheduler*>(ptr)->scheduleDelayed(std::move(evt), delay, prio);
        };
        _schedulePeriodicFn = [](void* ptr, EventVariant evt, std::chrono::microseconds interval, EventPriority prio) {
            return static_cast<Scheduler*>(ptr)->schedulePeriodic(std::move(evt), interval, prio);
        };
        _cancelTimerFn = [](void* ptr, TimerId id) {
            return static_cast<Scheduler*>(ptr)->cancelTimer(id);
        };
    }

    /// @brief Access reference to the event bus.
    [[nodiscard]] EventBusType& events() const
    {
        return *_events;
    }

    /// @brief Access event sink handle.
    [[nodiscard]] EventSinkT<EventVariant> eventSink() const
    {
        return _events->sink();
    }

    /// @brief Schedule a single-shot delayed event with std::chrono duration.
    template <typename Rep, typename Period>
    TimerId scheduleDelayed(EventVariant event, const std::chrono::duration<Rep, Period>& delay, EventPriority priority = EventPriority::Normal) const
    {
        if (_scheduleDelayedFn && _timerSchedulerPtr) {
            return _scheduleDelayedFn(_timerSchedulerPtr, std::move(event), std::chrono::duration_cast<std::chrono::microseconds>(delay), priority);
        }
        return INVALID_TIMER_ID;
    }

    /// @brief Schedule a recurring periodic event with std::chrono duration.
    template <typename Rep, typename Period>
    TimerId schedulePeriodic(EventVariant event, const std::chrono::duration<Rep, Period>& interval, EventPriority priority = EventPriority::Normal) const
    {
        if (_schedulePeriodicFn && _timerSchedulerPtr) {
            return _schedulePeriodicFn(_timerSchedulerPtr, std::move(event), std::chrono::duration_cast<std::chrono::microseconds>(interval), priority);
        }
        return INVALID_TIMER_ID;
    }

    /// @brief Cancel an active timer.
    bool cancelTimer(TimerId id) const noexcept
    {
        if (_cancelTimerFn && _timerSchedulerPtr) {
            return _cancelTimerFn(_timerSchedulerPtr, id);
        }
        return false;
    }

    /// @brief Request graceful application exit.
    void requestQuit() const
    {
        if (_quitCallback) {
            _quitCallback();
        }
    }

    explicit operator bool() const noexcept
    {
        return _events != nullptr;
    }

private:
    EventBusType* _events = nullptr;
    StaticCallback _quitCallback;

    void* _timerSchedulerPtr = nullptr;
    ScheduleDelayedFn _scheduleDelayedFn = nullptr;
    SchedulePeriodicFn _schedulePeriodicFn = nullptr;
    CancelTimerFn _cancelTimerFn = nullptr;
};

} // namespace corium
