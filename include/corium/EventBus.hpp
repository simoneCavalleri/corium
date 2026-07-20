#pragma once

#include "corium/IEventSink.hpp"
#include "corium/internal/CallableTraits.hpp"
#include "corium/internal/EventQueue.hpp"
#include "corium/internal/Reactor.hpp"
#include "corium/policies/Policies.hpp"

#include <functional>
#include <utility>

namespace corium {

/// @brief Non-templated abstract base class for event bus routing and dispatching.
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
class EventBusBaseT : public IEventSinkT<EventVariant> {
public:
    virtual ~EventBusBaseT() = default;

    /// @brief Process a single pending event from the queue.
    /// @return true if an event was popped and dispatched; false if queue was empty.
    virtual bool processOne() = 0;

    /// @brief Check if event queue is empty.
    [[nodiscard]] virtual bool empty() const = 0;

    /// @brief Seal event handler registrations after initialization phase.
    virtual void seal() = 0;

    /// @brief Set callback triggered when queue transitions from empty to non-empty.
    /// @param callback Function to invoke on empty -> non-empty transition.
    virtual void setOnQueueNonEmpty(std::function<void()> callback) = 0;

    /// @brief Register an event handler with explicit event type parameter.
    /// @tparam EventType Event type to handle.
    /// @tparam Handler Callable handler type.
    /// @param handler Callback to invoke when event occurs.
    template <typename EventType, typename Handler>
    void registerHandler(Handler&& handler) {
        _reactor.template registerHandler<EventType>(std::forward<Handler>(handler));
    }

    /// @brief Register an event handler with automatic event type deduction.
    /// @tparam Handler Callable handler type (lambda, function pointer, or functor).
    /// @param handler Callback to invoke when event occurs.
    template <typename Handler>
    void registerHandler(Handler&& handler) {
        using EventType = callable_event_type_t<Handler>;
        _reactor.template registerHandler<EventType>(std::forward<Handler>(handler));
    }

protected:
    ReactorT<EventVariant> _reactor;
};

/// @brief Default EventBusBase alias using DefaultEvents.
using EventBusBase = EventBusBaseT<DefaultEvents>;

/// @brief Policy-configurable event bus implementation.
/// @tparam EventVariant The variant type list of supported events.
/// @tparam QueuePolicy Strategy for queueing events (bounded lock-free MPSC, blocking queue, etc.).
/// @tparam SignalPolicy Strategy for thread signaling (callback, futex atomic wait, polling, etc.).
/// @tparam DispatchPolicy Strategy for event dispatching.
template <
    typename EventVariant = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariant, 1024>,
    typename SignalPolicy = CallbackSignalPolicy,
    typename DispatchPolicy = StaticReactorPolicy<EventVariant>
>
class BasicEventBus : public EventBusBaseT<EventVariant> {
public:
    BasicEventBus() = default;

    /// @brief Post an event into the queue.
    void post(EventVariant event) override
    {
        _eventQueue.pushEvent(std::move(event));
    }

    /// @brief Process a single event from the queue.
    bool processOne() override
    {
        auto eventOpt = _eventQueue.tryPopEvent();
        if (!eventOpt) {
            return false;
        }
        this->_reactor.dispatch(*eventOpt);
        return true;
    }

    /// @brief Check if event queue is empty.
    [[nodiscard]] bool empty() const override
    {
        return _eventQueue.empty();
    }

    /// @brief Seal reactor handlers.
    void seal() override
    {
        this->_reactor.seal();
    }

    /// @brief Set callback for event availability when queue transitions to non-empty.
    void setOnQueueNonEmpty(std::function<void()> callback) override
    {
        _eventQueue.setOnQueueNonEmpty(std::move(callback));
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

private:
    EventQueue<QueuePolicy, SignalPolicy> _eventQueue;
};

/// @brief Default EventBus alias using DefaultEvents.
using EventBus = BasicEventBus<DefaultEvents>;

} // namespace corium
