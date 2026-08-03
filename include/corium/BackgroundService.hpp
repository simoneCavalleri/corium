#pragma once

#include "corium/EventBus.hpp"
#include "corium/IEventSink.hpp"
#include "corium/ServiceContext.hpp"
#include "corium/policies/SignalPolicies.hpp"

#include <chrono>
#include <stop_token>
#include <thread>
#include <utility>

namespace corium {

/// @brief Non-allocating base class for background services owning a dedicated C++20 std::jthread.
/// Supports both event producing (posting to main EventBus) and event consuming (receiving events into dedicated incoming bus).
/// Zero heap allocations, zero vtables/RTTI.
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
class BackgroundService {
public:
    using EventVariant = EventVariantType;
    using IncomingBus = BasicEventBus<EventVariant, QueuePolicy, SignalPolicy, StoragePolicy, OverflowPolicy>;

    BackgroundService() = default;

    explicit BackgroundService(ServiceContextT<EventVariant> context)
        : _context(context)
    {}

    ~BackgroundService()
    {
        stop();
        join();
    }

    BackgroundService(const BackgroundService&) = delete;
    BackgroundService& operator=(const BackgroundService&) = delete;

    void setContext(ServiceContextT<EventVariant> context) noexcept
    {
        _context = context;
    }

    /// @brief Get an IEventSinkT handle targeting this service's incoming event queue.
    [[nodiscard]] IEventSinkT<EventVariant> sink() noexcept
    {
        return _incomingBus.sink();
    }

    /// @brief Get an IEventSinkT handle targeting this service's incoming event queue (alias).
    [[nodiscard]] IEventSinkT<EventVariant> serviceSink() noexcept
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

    /// @brief Register an event handler for incoming events with automatic event type deduction (alias).
    template <typename Handler>
    bool handle(Handler&& handler)
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

    /// @brief Wait for incoming events or timeout, then pump all available incoming events.
    /// Safe for use inside worker thread run(std::stop_token).
    /// @tparam Rep Duration representation type.
    /// @tparam Period Duration period type.
    /// @param stopToken std::stop_token from run(stopToken).
    /// @param timeout Maximum duration to wait if queue is empty.
    /// @return Number of events processed.
    template <typename Rep, typename Period>
    std::size_t waitAndPump(const std::stop_token& stopToken, const std::chrono::duration<Rep, Period>& timeout)
    {
        if (_incomingBus.empty() && !stopToken.stop_requested()) {
            _incomingBus.signalPolicy().wait_for(timeout);
        }

        std::size_t processed = 0;
        while (!stopToken.stop_requested()) {
            if (!_incomingBus.processOne()) {
                break;
            }
            processed++;
        }
        return processed;
    }

    /// @brief Start execution loop on dedicated std::jthread.
    template <typename Derived>
    void startThread(Derived* derived)
    {
        _thread = std::jthread([this, derived](std::stop_token stopToken) {
#if __cpp_exceptions
            try {
                derived->run(stopToken);
            } catch (...) {
            }
#else
            derived->run(stopToken);
#endif
        });
    }

    /// @brief Request graceful stop of the background thread via std::stop_token.
    void stop() noexcept
    {
        _thread.request_stop();
    }

    /// @brief Join background std::jthread cleanly.
    void join() noexcept
    {
        if (_thread.joinable()) {
            _thread.join();
        }
    }

protected:
    [[nodiscard]] ServiceContextT<EventVariant>& context() noexcept { return _context; }
    [[nodiscard]] const ServiceContextT<EventVariant>& context() const noexcept { return _context; }

    [[nodiscard]] IEventSinkT<EventVariant> events() const noexcept { return _context.eventSink; }
    [[nodiscard]] IEventSinkT<EventVariant> mainEventSink() const noexcept { return _context.eventSink; }

    template <typename EventType>
    void postEvent(EventType&& event) const
    {
        _context.eventSink.post(std::forward<EventType>(event));
    }

    template <typename TargetService, typename EventType>
    bool sendToService(EventType&& event, EventPriority priority = EventPriority::Normal) const
    {
        return _context.template sendToService<TargetService>(std::forward<EventType>(event), priority);
    }

private:
    ServiceContextT<EventVariant> _context;
    IncomingBus _incomingBus;
    std::jthread _thread;
};

} // namespace corium
