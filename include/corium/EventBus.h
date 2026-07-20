#pragma once

#include "corium/IEventSink.h"
#include "corium/internal/EventQueue.h"
#include "corium/internal/Reactor.h"

#include <functional>
#include <utility>

namespace corium {

class EventBusBase : public IEventSink {
public:
    virtual ~EventBusBase() = default;

    virtual bool processOne() = 0;
    virtual void seal() = 0;
    virtual void setOnEventsAvailable(std::function<void()> callback) = 0;

    template <typename EventType, typename Handler>
    void registerHandler(Handler&& handler) {
        _reactor.registerHandler<EventType>(std::forward<Handler>(handler));
    }

protected:
    Reactor _reactor;
};

template <size_t Capacity = 1024>
class BasicEventBus : public EventBusBase {
public:
    BasicEventBus() = default;

    void post(Event event) override
    {
        _eventQueue.pushEvent(std::move(event));
    }

    bool processOne() override
    {
        auto eventOpt = _eventQueue.tryPopEvent();
        if (!eventOpt) {
            return false;
        }
        _reactor.dispatch(*eventOpt);
        return true;
    }

    void seal() override
    {
        _reactor.seal();
    }

    void setOnEventsAvailable(std::function<void()> callback) override
    {
        _eventQueue.setOnEventsAvailable(std::move(callback));
    }

private:
    EventQueue<Capacity> _eventQueue;
};

using EventBus = BasicEventBus<1024>;

} // namespace corium
