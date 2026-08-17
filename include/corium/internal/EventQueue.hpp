#pragma once

#include <optional>
#include <utility>

#include "corium/Events.hpp"
#include "corium/policies/Policies.hpp"

namespace corium {

/// @brief Event queue composing QueuePolicy, SignalPolicy, and OverflowPolicy strategy types.
/// @tparam QueuePolicy Queueing policy strategy.
/// @tparam SignalPolicy Signaling policy strategy.
/// @tparam OverflowPolicy Strategy for handling queue overflow when capacity is exceeded.
template <
    typename QueuePolicy = BoundedMpscQueuePolicy<DefaultEvents, 1024>,
    typename SignalPolicy = NoSignalPolicy,
    typename OverflowPolicy = DropNewestOverflowPolicy
>
class EventQueue {
public:
    using EventVariant = typename QueuePolicy::EventType;

    EventQueue() = default;

    /// @brief Push an event into the queue and trigger signal policy on 0->1 transition (rvalue overload).
    bool pushEvent(EventVariant&& event, EventPriority priority = EventPriority::Normal)
    {
        auto res = _queuePolicy.tryPush(std::move(event), priority);
        if (!res.pushed) {
            return _overflowPolicy.handleOverflow(_queuePolicy, std::move(event), priority);
        }

        if (res.wasEmpty) {
            _signalPolicy.signal();
        }

        return true;
    }

    /// @brief Push an event into the queue (const lvalue overload).
    bool pushEvent(const EventVariant& event, EventPriority priority = EventPriority::Normal)
    {
        EventVariant copy = event;
        return pushEvent(std::move(copy), priority);
    }

    /// @brief Pop an event directly into output reference without intermediate optional.
    bool tryPopEvent(EventVariant& event)
    {
        return _queuePolicy.tryPop(event);
    }

    /// @brief Pop an event from the queue.
    /// @return Optional containing popped event, or std::nullopt if empty.
    std::optional<EventVariant> tryPopEvent()
    {
        EventVariant event;
        if (_queuePolicy.tryPop(event)) {
            return event;
        }
        return std::nullopt;
    }

    /// @brief Check if event queue is empty.
    [[nodiscard]] bool empty() const
    {
        return _queuePolicy.empty();
    }

    /// @brief Set non-allocating static callback triggered when queue transitions from empty to non-empty.
    void setOnQueueNonEmpty(StaticCallback callback)
    {
        _signalPolicy.setOnQueueNonEmpty(callback);
    }

    /// @brief Access reference to signal policy.
    SignalPolicy& signalPolicy() noexcept
    {
        return _signalPolicy;
    }

    /// @brief Access const reference to signal policy.
    const SignalPolicy& signalPolicy() const noexcept
    {
        return _signalPolicy;
    }

    /// @brief Access reference to overflow policy.
    OverflowPolicy& overflowPolicy() noexcept
    {
        return _overflowPolicy;
    }

    /// @brief Access const reference to overflow policy.
    const OverflowPolicy& overflowPolicy() const noexcept
    {
        return _overflowPolicy;
    }

private:
    [[no_unique_address]] QueuePolicy _queuePolicy;
    [[no_unique_address]] SignalPolicy _signalPolicy;
    [[no_unique_address]] OverflowPolicy _overflowPolicy;
};

} // namespace corium
