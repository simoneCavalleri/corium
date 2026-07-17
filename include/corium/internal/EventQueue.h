#pragma once

#include <mutex>
#include <queue>
#include <optional>

#include "corium/Events.h"

namespace corium {

class EventQueue {
public:
    void pushEvent(Event event);
    std::optional<Event> tryPopEvent();

private:
    std::queue<Event> _events;
    std::mutex _mutex;
};

} // namespace corium
