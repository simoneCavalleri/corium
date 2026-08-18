/**
 * @file ApplicationContext.hpp
 * @ingroup core
 * @brief Type-erased context for application runtime introspection and lifecycle control.
 */

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>

#include "corium/Events.hpp"
#include "corium/EventSink.hpp"
#include "corium/internal/CallableTraits.hpp"
#include "corium/internal/FastDelegate.hpp"
#include "corium/internal/VariantIndex.hpp"
#include "corium/policies/QueuePolicies.hpp"
#include "corium/policies/SignalPolicies.hpp"
#include "corium/timers/TimerScheduler.hpp"

namespace corium {

/// @ingroup core
/// @brief Context object passed to Application providing event registration, sink access, quit requests, and timer scheduling.
/// Completely encapsulates EventBus and Reactor internals.
/// @tparam EventVariant Supported event variant list type (defaults to DefaultEvents).
template <typename EventVariant = DefaultEvents>
class ApplicationContext {
    static constexpr std::size_t NumEvents = std::variant_size_v<EventVariant>;

    using RegFn = bool (*)(
        void* busPtr,
        void* handlerObj,
        void* invokerFn,
        void (*mover)(void* destStorage, void*& destInstance, void*& srcInstance) noexcept,
        void (*destroyer)(void* instance) noexcept,
        std::size_t size
    );

public:
    using EventVariantType = EventVariant;

    using ScheduleDelayedFn = TimerId (*)(void* ptr, EventVariant event, std::chrono::microseconds delay, EventPriority priority);
    using SchedulePeriodicFn = TimerId (*)(void* ptr, EventVariant event, std::chrono::microseconds interval, EventPriority priority);
    using CancelTimerFn = bool (*)(void* ptr, TimerId id);

    ApplicationContext() = default;

    template <typename EventBusType>
    ApplicationContext(EventBusType& events, StaticCallback quitCallback)
        : _busPtr(&events), _eventSink(events.sink()), _quitCallback(quitCallback)
    {
        initRegFns<EventBusType>(std::make_index_sequence<NumEvents>{});
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

    /// @brief Register an event handler into the application event bus.
    template <typename Handler>
    bool registerHandler(Handler&& handler)
    {
        using EventType = callable_event_type_t<Handler>;
        static_assert(has_variant_type_v<EventType, EventVariant>, "EventType is not part of Application's EventVariant list!");
        constexpr std::size_t typeIdx = variant_index_v<EventType, EventVariant>;

        if (_busPtr && _regFns[typeIdx]) {
            using Decayed = std::decay_t<Handler>;
            Decayed h(std::forward<Handler>(handler));

            void (*invoker)(void* instance, const EventType& event) = [](void* instance, const EventType& event) {
                (*static_cast<Decayed*>(instance))(event);
            };

            auto mover = [](void* destStorage, void*& destInstance, void*& srcInstance) noexcept {
                auto* src = static_cast<Decayed*>(srcInstance);
                ::new (destStorage) Decayed(std::move(*src));
                src->~Decayed();
                destInstance = destStorage;
                srcInstance = nullptr;
            };

            auto destroyer = [](void* instance) noexcept {
                static_cast<Decayed*>(instance)->~Decayed();
            };

            void* srcPtr = &h;
            return _regFns[typeIdx](_busPtr, srcPtr, reinterpret_cast<void*>(invoker), mover, destroyer, sizeof(Decayed));
        }
        return false;
    }

    /// @brief Register a filtered event handler executed only when predicate evaluates to true.
    /// @tparam Filter Callable returning bool when passed the event.
    /// @tparam Handler Callable accepting const EventType&.
    template <typename Filter, typename Handler>
    bool registerFilteredHandler(Filter&& filter, Handler&& handler)
    {
        using EventType = callable_event_type_t<Handler>;
        return registerHandler([f = std::forward<Filter>(filter), h = std::forward<Handler>(handler)](const EventType& event) {
            if (f(event)) {
                h(event);
            }
        });
    }

    /// @brief Access event sink handle.
    [[nodiscard]] EventSinkT<EventVariant> eventSink() const noexcept
    {
        return _eventSink;
    }

    /// @brief Schedule a single-shot delayed event with std::chrono duration.
    template <typename Rep, typename Period>
    [[nodiscard]] TimerId scheduleDelayed(EventVariant event, const std::chrono::duration<Rep, Period>& delay, EventPriority priority = EventPriority::Normal) const
    {
        if (_scheduleDelayedFn && _timerSchedulerPtr) {
            return _scheduleDelayedFn(_timerSchedulerPtr, std::move(event), std::chrono::duration_cast<std::chrono::microseconds>(delay), priority);
        }
        return INVALID_TIMER_ID;
    }

    /// @brief Schedule a recurring periodic event with std::chrono duration.
    template <typename Rep, typename Period>
    [[nodiscard]] TimerId schedulePeriodic(EventVariant event, const std::chrono::duration<Rep, Period>& interval, EventPriority priority = EventPriority::Normal) const
    {
        if (_schedulePeriodicFn && _timerSchedulerPtr) {
            return _schedulePeriodicFn(_timerSchedulerPtr, std::move(event), std::chrono::duration_cast<std::chrono::microseconds>(interval), priority);
        }
        return INVALID_TIMER_ID;
    }

    /// @brief Cancel an active timer.
    [[nodiscard]] bool cancelTimer(TimerId id) const noexcept
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

    /// @brief Attach runtime detachment handle.
    void setRuntimeDetach(void* runtimePtr, void (*detachFn)(void*) noexcept) noexcept
    {
        _runtimePtr = runtimePtr;
        _detachFn = detachFn;
    }

    /// @brief Detach application from runtime to prevent dangling callbacks on shutdown.
    void detachFromRuntime() noexcept
    {
        if (_detachFn && _runtimePtr) {
            _detachFn(_runtimePtr);
            _detachFn = nullptr;
            _runtimePtr = nullptr;
        }
    }

    /// @brief Reset context state to empty.
    void reset() noexcept
    {
        _busPtr = nullptr;
        _runtimePtr = nullptr;
        _detachFn = nullptr;
        _timerSchedulerPtr = nullptr;
        _scheduleDelayedFn = nullptr;
        _schedulePeriodicFn = nullptr;
        _cancelTimerFn = nullptr;
        _quitCallback = StaticCallback{};
    }

    explicit operator bool() const noexcept
    {
        return _busPtr != nullptr;
    }

private:
    template <typename EventBusType, std::size_t... Is>
    void initRegFns(std::index_sequence<Is...>) noexcept
    {
        ((_regFns[Is] = [](
            void* bPtr,
            void* handlerObj,
            void* invokerFn,
            void (*mover)(void* destStorage, void*& destInstance, void*& srcInstance) noexcept,
            void (*destroyer)(void* instance) noexcept,
            std::size_t size
        ) -> bool {
            using EventType = std::variant_alternative_t<Is, EventVariant>;
            using StoragePolicy = typename EventBusType::ReactorType::StoragePolicyType;
            constexpr std::size_t InlineSize = StoragePolicy::inline_storage_size;

            if (size > InlineSize) {
                return false;
            }

            auto* bus = static_cast<EventBusType*>(bPtr);
            auto stub = reinterpret_cast<void (*)(void*, const EventType&)>(invokerFn);

            EventHandlerDelegate<EventType, InlineSize> del(handlerObj, stub, mover, destroyer);
            return bus->template registerHandler<EventType>(std::move(del));
        }), ...);
    }

    void* _busPtr = nullptr;
    EventSinkT<EventVariant> _eventSink{};
    StaticCallback _quitCallback{};
    std::array<RegFn, NumEvents> _regFns{};

    void* _timerSchedulerPtr = nullptr;
    ScheduleDelayedFn _scheduleDelayedFn = nullptr;
    SchedulePeriodicFn _schedulePeriodicFn = nullptr;
    CancelTimerFn _cancelTimerFn = nullptr;

    void* _runtimePtr = nullptr;
    void (*_detachFn)(void*) noexcept = nullptr;
};

} // namespace corium
