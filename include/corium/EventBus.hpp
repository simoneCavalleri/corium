#pragma once

#include "corium/EventSink.hpp"
#include "corium/internal/CallableTraits.hpp"
#include "corium/internal/EventQueue.hpp"
#include "corium/internal/Reactor.hpp"
#include "corium/policies/Policies.hpp"
#include "corium/profiler/ProfilerPolicies.hpp"

#include <utility>

namespace corium {

/// @ingroup core
/// @brief Policy-configurable non-virtual event bus implementation.
/// @tparam EventVariantType The variant type list of supported events.
/// @tparam QueuePolicy Strategy for queueing events (bounded lock-free MPSC).
/// @tparam SignalPolicy Strategy for signaling (NoSignalPolicy by default).
/// @tparam StoragePolicy Strategy for compile-time handler capacity and delegate storage.
/// @tparam OverflowPolicy Strategy for queue overflow handling.
/// @tparam ProfilerPolicy Strategy for latency telemetry and trace recording (NullProfiler by default).
template <
    typename EventVariantType = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariantType, 1024>,
    typename SignalPolicy = NoSignalPolicy,
    typename StoragePolicy = DefaultStoragePolicy,
    typename OverflowPolicy = DropNewestOverflowPolicy,
    typename ProfilerPolicy = profiler::NullProfiler
>
class BasicEventBus {
public:
    using EventVariant = EventVariantType;
    using ReactorType = BasicReactor<EventVariant, StoragePolicy>;
    using ProfilerPolicyType = ProfilerPolicy;

    BasicEventBus() = default;

    /// @brief Post an event into the queue with optional priority (rvalue overload).
    void post(EventVariant&& event, EventPriority priority = EventPriority::Normal)
    {
        _profilerPolicy.onEventPosted(event, static_cast<uint8_t>(priority));
        _profilerPolicy.recordPostTime(_profilerPolicy.nowNs());
        _eventQueue.pushEvent(std::move(event), priority);
    }

    /// @brief Post an event into the queue with optional priority (const lvalue overload).
    void post(const EventVariant& event, EventPriority priority = EventPriority::Normal)
    {
        _profilerPolicy.onEventPosted(event, static_cast<uint8_t>(priority));
        _profilerPolicy.recordPostTime(_profilerPolicy.nowNs());
        _eventQueue.pushEvent(event, priority);
    }

    /// @brief Convenience helper for posting high-priority events.
    template <typename Event>
    void postHighPriority(Event&& event)
    {
        post(EventVariant(std::forward<Event>(event)), EventPriority::High);
    }

    /// @brief Process a single event from the queue.
    /// @return true if an event was popped and dispatched; false if queue was empty.
    bool processOne()
    {
        EventVariant event;
        if (!_eventQueue.tryPopEvent(event)) {
            return false;
        }
        const uint64_t postTime     = _profilerPolicy.takePostTime();
        const uint64_t dispatchTime = _profilerPolicy.nowNs();
        _reactor.dispatch(event);
        const uint64_t finishTime = _profilerPolicy.nowNs();
        _profilerPolicy.onEventDispatched(event, 0, postTime, dispatchTime, finishTime);
        return true;
    }

    /// @brief Process up to maxBatch events consecutively from the queue.
    /// @param maxBatch Maximum number of events to process in this batch.
    /// @return Number of events successfully popped and dispatched.
    std::size_t processBatch(std::size_t maxBatch)
    {
        std::size_t count = 0;
        EventVariant event;
        while (count < maxBatch) {
            if (!_eventQueue.tryPopEvent(event)) {
                break;
            }
            const uint64_t postTime     = _profilerPolicy.takePostTime();
            const uint64_t dispatchTime = _profilerPolicy.nowNs();
            _reactor.dispatch(event);
            const uint64_t finishTime = _profilerPolicy.nowNs();
            _profilerPolicy.onEventDispatched(event, 0, postTime, dispatchTime, finishTime);
            count++;
        }
        return count;
    }

    /// @brief Drain and dispatch all currently enqueued events.
    /// @return Total number of events processed.
    std::size_t drain()
    {
        std::size_t total = 0;
        while (processOne()) {
            total++;
        }
        return total;
    }

    /// @brief Access reference to profiler policy.
    [[nodiscard]] ProfilerPolicy& profiler() noexcept
    {
        return _profilerPolicy;
    }

    /// @brief Access const reference to profiler policy.
    [[nodiscard]] const ProfilerPolicy& profiler() const noexcept
    {
        return _profilerPolicy;
    }

    /// @brief Check if event queue is empty.
    [[nodiscard]] bool empty() const
    {
        return _eventQueue.empty();
    }

    /// @brief Seal reactor handlers.
    void seal()
    {
        _reactor.seal();
    }

    /// @brief Set static callback for event availability when queue transitions to non-empty.
    void setOnQueueNonEmpty(StaticCallback callback)
    {
        _eventQueue.setOnQueueNonEmpty(callback);
    }

    /// @brief Register an event handler with explicit event type parameter.
    /// @tparam EventType Event type to handle.
    /// @tparam Handler Callable handler type.
    /// @param handler Callback to invoke when event occurs.
    template <typename EventType, typename Handler>
    bool registerHandler(Handler&& handler)
    {
        return _reactor.template registerHandler<EventType>(std::forward<Handler>(handler));
    }

    /// @brief Register an event handler with automatic event type deduction.
    /// @tparam Handler Callable handler type (lambda, function pointer, or functor).
    /// @param handler Callback to invoke when event occurs.
    template <typename Handler>
    bool registerHandler(Handler&& handler)
    {
        using EventType = callable_event_type_t<Handler>;
        return _reactor.template registerHandler<EventType>(std::forward<Handler>(handler));
    }

    /// @brief Access reference to signal policy.
    [[nodiscard]] SignalPolicy& signalPolicy() noexcept
    {
        return _eventQueue.signalPolicy();
    }

    /// @brief Access const reference to signal policy.
    [[nodiscard]] const SignalPolicy& signalPolicy() const noexcept
    {
        return _eventQueue.signalPolicy();
    }

    /// @brief Access reference to overflow policy.
    [[nodiscard]] OverflowPolicy& overflowPolicy() noexcept
    {
        return _eventQueue.overflowPolicy();
    }

    /// @brief Access const reference to overflow policy.
    [[nodiscard]] const OverflowPolicy& overflowPolicy() const noexcept
    {
        return _eventQueue.overflowPolicy();
    }

    /// @brief Access reference to reactor.
    ReactorType& reactor() noexcept
    {
        return _reactor;
    }

    /// @brief Get an EventSink handle pointing to this event bus.
    EventSinkT<EventVariant> sink() noexcept
    {
        return EventSinkT<EventVariant>(*this);
    }

private:
    EventQueue<QueuePolicy, SignalPolicy, OverflowPolicy> _eventQueue;
    ReactorType _reactor;
    ProfilerPolicy _profilerPolicy{};
};

/// @brief Default EventBus alias using DefaultEvents and NoSignalPolicy.
using EventBus = BasicEventBus<DefaultEvents>;

/// @brief Templated EventBus alias for custom event variant and policies.
template <
    typename EventVariantType = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariantType, 1024>,
    typename SignalPolicy = NoSignalPolicy,
    typename StoragePolicy = DefaultStoragePolicy,
    typename OverflowPolicy = DropNewestOverflowPolicy,
    typename ProfilerPolicy = profiler::NullProfiler
>
using EventBusT = BasicEventBus<EventVariantType, QueuePolicy, SignalPolicy, StoragePolicy, OverflowPolicy, ProfilerPolicy>;

} // namespace corium
