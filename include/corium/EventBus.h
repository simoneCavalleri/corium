#pragma once

#include "corium/IEventSink.h"
#include "corium/internal/EventQueue.h"
#include "corium/internal/Reactor.h"

namespace corium {

class EventBus : public IEventSink {
public:
    EventBus() = default;

    void post(Event event) override;

    bool processOne();

    void seal();

    void setOnEventsAvailable(std::function<void()> callback) {
        _eventQueue.setOnEventsAvailable(std::move(callback));
    }

    template <typename EventType, typename Handler>
    void registerHandler(Handler&& handler) {
        _reactor.registerHandler<EventType>(std::forward<Handler>(handler));
    }

private:
    EventQueue _eventQueue;
    Reactor _reactor;
};

} // namespace corium

