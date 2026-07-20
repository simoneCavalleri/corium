#pragma once

#include <functional>
#include <mutex>
#include <optional>

#include "corium/Events.h"
#include "corium/internal/MpscRingBuffer.h"

namespace corium {

class EventQueue {
public:
    bool pushEvent(Event event);
    std::optional<Event> tryPopEvent();

    void setOnEventsAvailable(std::function<void()> callback);

private:
    MpscRingBuffer<Event, 1024> _ringBuffer;
    std::mutex _callbackMutex;
    std::function<void()> _onEventsAvailable;
};

} // namespace corium
