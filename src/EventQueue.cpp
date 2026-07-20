#include "corium/internal/EventQueue.h"

#include <utility>

namespace corium {

void EventQueue::setOnEventsAvailable(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(_mutex);
    _onEventsAvailable = std::move(callback);
}

void EventQueue::pushEvent(Event event) {
    std::function<void()> callbackToInvoke;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        bool wasEmpty = _events.empty();
        _events.push(std::move(event));
        if (wasEmpty && _onEventsAvailable) {
            callbackToInvoke = _onEventsAvailable;
        }
    }

    if (callbackToInvoke) {
        callbackToInvoke();
    }
}

std::optional<Event> EventQueue::tryPopEvent() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_events.empty()) {
        return std::nullopt;
    }

    Event event = std::move(_events.front());
    _events.pop();
    return event;
}

} // namespace corium
