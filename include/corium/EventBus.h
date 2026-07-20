#pragma once

#include "corium/IEventSink.h"
#include "corium/internal/EventQueue.h"
#include "corium/internal/Reactor.h"

#include <functional>
#include <utility>

namespace corium {

template <typename EventVariant = DefaultEvents>
class EventBusBaseT : public IEventSinkT<EventVariant> {
public:
    virtual ~EventBusBaseT() = default;

    virtual bool processOne() = 0;
    virtual void seal() = 0;
    virtual void setOnEventsAvailable(std::function<void()> callback) = 0;

    template <typename EventType, typename Handler>
    void registerHandler(Handler&& handler) {
        _reactor.template registerHandler<EventType>(std::forward<Handler>(handler));
    }

protected:
    ReactorT<EventVariant> _reactor;
};

using EventBusBase = EventBusBaseT<DefaultEvents>;

template <typename EventVariant = DefaultEvents, size_t Capacity = 1024>
class BasicEventBus : public EventBusBaseT<EventVariant> {
public:
    BasicEventBus() = default;

    void post(EventVariant event) override
    {
        _eventQueue.pushEvent(std::move(event));
    }

    bool processOne() override
    {
        auto eventOpt = _eventQueue.tryPopEvent();
        if (!eventOpt) {
            return false;
        }
        this->_reactor.dispatch(*eventOpt);
        return true;
    }

    void seal() override
    {
        this->_reactor.seal();
    }

    void setOnEventsAvailable(std::function<void()> callback) override
    {
        _eventQueue.setOnEventsAvailable(std::move(callback));
    }

private:
    EventQueue<EventVariant, Capacity> _eventQueue;
};

using EventBus = BasicEventBus<DefaultEvents, 1024>;

} // namespace corium
