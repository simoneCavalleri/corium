#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <utility>

#include "corium/Events.h"
#include "corium/internal/MpscRingBuffer.h"

namespace corium {

template <typename EventVariant = DefaultEvents, size_t Capacity = 1024>
class EventQueue {
public:
    EventQueue() = default;

    bool pushEvent(EventVariant event)
    {
        auto res = _ringBuffer.tryPush(std::move(event));
        if (!res.pushed) {
            return false;
        }

        if (res.wasEmpty) {
            std::function<void()> callbackToInvoke;
            {
                std::lock_guard<std::mutex> lock(_callbackMutex);
                callbackToInvoke = _onEventsAvailable;
            }

            if (callbackToInvoke) {
                callbackToInvoke();
            }
        }

        return true;
    }

    std::optional<EventVariant> tryPopEvent()
    {
        EventVariant event;
        if (_ringBuffer.tryPop(event)) {
            return event;
        }
        return std::nullopt;
    }

    void setOnEventsAvailable(std::function<void()> callback)
    {
        std::lock_guard<std::mutex> lock(_callbackMutex);
        _onEventsAvailable = std::move(callback);
    }

private:
    MpscRingBuffer<EventVariant, Capacity> _ringBuffer;
    std::mutex _callbackMutex;
    std::function<void()> _onEventsAvailable;
};

} // namespace corium
