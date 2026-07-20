#include "corium/internal/EventQueue.h"

#include <utility>

namespace corium {

void EventQueue::setOnEventsAvailable(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(_callbackMutex);
    _onEventsAvailable = std::move(callback);
}

bool EventQueue::pushEvent(Event event) {
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

std::optional<Event> EventQueue::tryPopEvent() {
    Event event;
    if (_ringBuffer.tryPop(event)) {
        return event;
    }
    return std::nullopt;
}

} // namespace corium
