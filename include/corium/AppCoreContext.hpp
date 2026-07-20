#pragma once

#include <functional>
#include "corium/EventBus.hpp"
#include "corium/IEventSink.hpp"

namespace corium {

template <typename EventVariant = DefaultEvents>
class AppCoreContextT {
public:
    AppCoreContextT() = default;

    AppCoreContextT(EventBusBaseT<EventVariant>& events, IEventSinkT<EventVariant>& eventSink, std::function<void()> quitCallback)
        : _events(&events), _eventSink(&eventSink), _quitCallback(quitCallback)
    {
    }

    EventBusBaseT<EventVariant>& events() const
    {
        return *_events;
    }

    IEventSinkT<EventVariant>& eventSink() const
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
    EventBusBaseT<EventVariant>* _events = nullptr;
    IEventSinkT<EventVariant>* _eventSink = nullptr;
    std::function<void()> _quitCallback;
};

using AppCoreContext = AppCoreContextT<DefaultEvents>;

} // namespace corium
