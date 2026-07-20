#pragma once

#include <functional>
#include <optional>
#include <utility>

#include "corium/Events.h"
#include "corium/policies/Policies.h"

namespace corium {

template <
    typename QueuePolicy = BoundedMpscQueuePolicy<DefaultEvents, 1024>,
    typename SignalPolicy = CallbackSignalPolicy
>
class EventQueue {
public:
    using EventVariant = typename QueuePolicy::EventType;

    EventQueue() = default;

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

    std::optional<EventVariant> tryPopEvent()
    {
        EventVariant event;
        if (_queuePolicy.tryPop(event)) {
            return event;
        }
        return std::nullopt;
    }

    void setOnEventsAvailable(std::function<void()> callback)
    {
        _signalPolicy.setOnEventsAvailable(std::move(callback));
    }

    SignalPolicy& signalPolicy() noexcept
    {
        return _signalPolicy;
    }

    const SignalPolicy& signalPolicy() const noexcept
    {
        return _signalPolicy;
    }

private:
    QueuePolicy _queuePolicy;
    SignalPolicy _signalPolicy;
};

} // namespace corium
