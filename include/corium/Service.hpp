#pragma once

#include "corium/EventBus.hpp"
#include "corium/EventSink.hpp"
#include "corium/ServiceContext.hpp"
#include "corium/policies/QueuePolicies.hpp"
#include "corium/policies/SignalPolicies.hpp"
#include "corium/policies/StoragePolicies.hpp"

#include <cstddef>
#include <utility>

namespace corium {

/// @ingroup core
/// @brief Non-allocating base class for synchronous or thread-agnostic services.
/// Provides event producing (posting to main EventBus) and event consuming (receiving events into dedicated incoming bus).
/// Does NOT own a thread. Zero heap allocations, zero vtables/RTTI.
/// @tparam EventVariantType Supported event variant type list.
/// @tparam QueuePolicy Strategy for queueing incoming events (bounded lock-free MPSC).
/// @tparam SignalPolicy Strategy for signaling (CallbackSignalPolicy default).
/// @tparam StoragePolicy Strategy for compile-time handler capacity and delegate storage.
/// @tparam OverflowPolicy Strategy for queue overflow handling.
template <
    typename EventVariantType = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariantType, 1024>,
    typename SignalPolicy = CallbackSignalPolicy,
    typename StoragePolicy = DefaultStoragePolicy,
    typename OverflowPolicy = DropNewestOverflowPolicy
>
class Service {
public:
    using EventVariant = EventVariantType;
    using IncomingBus = BasicEventBus<EventVariant, QueuePolicy, SignalPolicy, StoragePolicy, OverflowPolicy>;

    Service() = default;

    explicit Service(ServiceContextT<EventVariant> context)
        : _context(context)
    {}

    ~Service() = default;

    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

    Service(Service&&) noexcept = default;
    Service& operator=(Service&&) noexcept = default;

    void setContext(ServiceContextT<EventVariant> context) noexcept
    {
        _context = context;
    }

    /// @brief Get an EventSink handle targeting this service's incoming event queue.
    [[nodiscard]] EventSinkT<EventVariant> sink() noexcept
    {
        return _incomingBus.sink();
    }

    /// @brief Register an event handler for incoming events with explicit event type parameter.
    template <typename EventType, typename Handler>
    bool registerHandler(Handler&& handler)
    {
        return _incomingBus.template registerHandler<EventType>(std::forward<Handler>(handler));
    }

    /// @brief Register an event handler for incoming events with automatic event type deduction.
    template <typename Handler>
    bool on(Handler&& handler)
    {
        return _incomingBus.registerHandler(std::forward<Handler>(handler));
    }

    /// @brief Process a single incoming event from the service queue.
    /// @return true if an event was popped and dispatched; false if queue was empty.
    bool processOne()
    {
        return _incomingBus.processOne();
    }

    /// @brief Pump all pending incoming events from the queue until empty.
    /// @return Number of events processed.
    std::size_t pump()
    {
        std::size_t processed = 0;
        while (_incomingBus.processOne()) {
            processed++;
        }
        return processed;
    }

    /// @brief Pump up to maxEvents pending incoming events from the queue.
    /// @param maxEvents Maximum number of events to process.
    /// @return Number of events processed.
    std::size_t pump(std::size_t maxEvents)
    {
        std::size_t processed = 0;
        while (processed < maxEvents && _incomingBus.processOne()) {
            processed++;
        }
        return processed;
    }

protected:
    [[nodiscard]] ServiceContextT<EventVariant>& context() noexcept { return _context; }
    [[nodiscard]] const ServiceContextT<EventVariant>& context() const noexcept { return _context; }

    /// @brief Access main application EventSink handle.
    [[nodiscard]] EventSinkT<EventVariant> mainSink() const noexcept { return _context.mainSink(); }

    /// @brief Post an event into the main application event queue.
    template <typename EventType>
    void post(EventType&& event, EventPriority priority = EventPriority::Normal) const
    {
        _context.mainSink().post(std::forward<EventType>(event), priority);
    }

    /// @brief Post a high-priority event into the main application event queue.
    template <typename EventType>
    void postHighPriority(EventType&& event) const
    {
        _context.mainSink().postHighPriority(std::forward<EventType>(event));
    }

    /// @brief Post an event directly to another registered service.
    template <typename TargetService, typename EventType>
    bool sendToService(EventType&& event, EventPriority priority = EventPriority::Normal) const
    {
        return _context.template sendToService<TargetService>(std::forward<EventType>(event), priority);
    }

    [[nodiscard]] IncomingBus& incomingBus() noexcept { return _incomingBus; }
    [[nodiscard]] const IncomingBus& incomingBus() const noexcept { return _incomingBus; }

private:
    ServiceContextT<EventVariant> _context;
    [[no_unique_address]] IncomingBus _incomingBus;
};

/// @brief Zero-overhead Service alias for pure producer services (no incoming event queue/reactor allocations).
/// @tparam EventVariant Supported event variant type list.
template <typename EventVariant = DefaultEvents>
using ProducerService = Service<
    EventVariant,
    NoQueuePolicy<EventVariant>,
    NoSignalPolicy,
    ZeroStoragePolicy
>;

/// @brief Explicit Service alias for consumer services with configurable incoming event queue capacity.
/// @tparam EventVariant Supported event variant type list.
/// @tparam Capacity Incoming ring buffer event capacity.
template <typename EventVariant = DefaultEvents, std::size_t Capacity = 64>
using ConsumerService = Service<
    EventVariant,
    BoundedMpscQueuePolicy<EventVariant, Capacity>,
    CallbackSignalPolicy,
    DefaultStoragePolicy
>;

} // namespace corium
