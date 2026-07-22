#pragma once

#include <optional>
#include <utility>

#include "corium/Events.hpp"
#include "corium/policies/Policies.hpp"

namespace corium {

/// @brief Event queue composing QueuePolicy and SignalPolicy strategy types.
/// @tparam QueuePolicy Queueing policy strategy.
/// @tparam SignalPolicy Signaling policy strategy.
template <
    typename QueuePolicy = BoundedMpscQueuePolicy<DefaultEvents, 1024>,
    typename SignalPolicy = NoSignalPolicy
>
class EventQueue {
public:
    using EventVariant = typename QueuePolicy::EventType;

    EventQueue() = default;

    /// @brief Push an event into the queue and trigger signal policy on 0->1 transition.
    /// @param event Event instance to push.
    /// @return true if event was pushed successfully; false if queue was full.
    bool pushEvent(EventVariant event)
    {
        auto res = _queuePolicy.tryPush(std::move(event));
        if (!res.pushed) {
            return false;
        }

        if (res.wasEmpty) {
            _signalPolicy.signal();
        }

        return true;
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

private:
    QueuePolicy _queuePolicy;
    SignalPolicy _signalPolicy;
};

} // namespace corium
