#include "corium/internal/EventQueue.h"

#include <utility>

namespace corium {

void EventQueue::pushEvent(Event event) {
    std::lock_guard<std::mutex> lock(_mutex);
    _events.push(std::move(event));
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
