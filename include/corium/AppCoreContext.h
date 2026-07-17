#pragma once

#include <functional>
#include "corium/EventBus.h"
#include "corium/IEventSink.h"

namespace corium {

class AppCoreContext {
public:
    AppCoreContext() = default;

    AppCoreContext(EventBus& events, IEventSink& eventSink, std::function<void()> quitCallback)
        : _events(&events), _eventSink(&eventSink), _quitCallback(quitCallback)
    {
    }

    EventBus& events() const
    {
        return *_events;
    }

    IEventSink& eventSink() const
    {
        return *_eventSink;
    }

    void requestQuit() const
    {
        if (_quitCallback) {
            _quitCallback();
        }
    }

    explicit operator bool() const
    {
        return _events != nullptr && _eventSink != nullptr;
    }

private:
    EventBus* _events = nullptr;
    IEventSink* _eventSink = nullptr;
    std::function<void()> _quitCallback;
};

} // namespace corium
