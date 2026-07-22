#pragma once

#include "corium/IEventSink.hpp"
#include "corium/internal/CallableTraits.hpp"
#include "corium/internal/EventQueue.hpp"
#include "corium/internal/Reactor.hpp"
#include "corium/policies/Policies.hpp"

#include <utility>

namespace corium {

/// @brief Policy-configurable non-virtual event bus implementation.
/// @tparam EventVariantType The variant type list of supported events.
/// @tparam QueuePolicy Strategy for queueing events (bounded lock-free MPSC).
/// @tparam SignalPolicy Strategy for signaling (NoSignalPolicy by default).
/// @tparam StoragePolicy Strategy for compile-time handler capacity and delegate storage.
template <
    typename EventVariantType = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariantType, 1024>,
    typename SignalPolicy = NoSignalPolicy,
    typename StoragePolicy = DefaultStoragePolicy
>
class BasicEventBus {
public:
    using EventVariant = EventVariantType;
    using ReactorType = ReactorT<EventVariant, StoragePolicy>;

    BasicEventBus() = default;

    /// @brief Post an event into the queue.
    void post(EventVariant event)
    {
        _eventQueue.pushEvent(std::move(event));
    }

    /// @brief Process a single event from the queue.
    /// @return true if an event was popped and dispatched; false if queue was empty.
    bool processOne()
    {
        auto eventOpt = _eventQueue.tryPopEvent();
        if (!eventOpt) {
            return false;
        }
        _reactor.dispatch(*eventOpt);
        return true;
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
    SignalPolicy& signalPolicy() noexcept
    {
        return _eventQueue.signalPolicy();
    }

    /// @brief Access const reference to signal policy.
    const SignalPolicy& signalPolicy() const noexcept
    {
        return _eventQueue.signalPolicy();
    }

    /// @brief Access reference to reactor.
    ReactorType& reactor() noexcept
    {
        return _reactor;
    }

    /// @brief Get an IEventSinkT handle pointing to this event bus.
    IEventSinkT<EventVariant> sink() noexcept
    {
        return IEventSinkT<EventVariant>(*this);
    }

private:
    EventQueue<QueuePolicy, SignalPolicy> _eventQueue;
    ReactorType _reactor;
};

/// @brief Default EventBus alias using DefaultEvents and NoSignalPolicy.
using EventBus = BasicEventBus<DefaultEvents>;

} // namespace corium
