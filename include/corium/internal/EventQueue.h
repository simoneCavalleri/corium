#pragma once

#include <functional>
#include <mutex>
#include <queue>
#include <optional>

#include "corium/Events.h"

namespace corium {

class EventQueue {
public:
    void pushEvent(Event event);
    std::optional<Event> tryPopEvent();

    void setOnEventsAvailable(std::function<void()> callback);

private:
    std::queue<Event> _events;
    std::mutex _mutex;
    std::function<void()> _onEventsAvailable;
};

} // namespace corium
