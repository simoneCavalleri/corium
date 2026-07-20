#pragma once

#include <functional>
#include "corium/EventBus.hpp"
#include "corium/IEventSink.hpp"

namespace corium {

/// @brief Context object passed to AppCore providing event bus access and quit requests.
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
class AppCoreContextT {
public:
    AppCoreContextT() = default;

    AppCoreContextT(EventBusBaseT<EventVariant>& events, IEventSinkT<EventVariant>& eventSink, std::function<void()> quitCallback)
        : _events(&events), _eventSink(&eventSink), _quitCallback(quitCallback)
    {
    }

    /// @brief Access reference to the event bus.
    [[nodiscard]] EventBusBaseT<EventVariant>& events() const
    {
        return *_events;
    }

    /// @brief Access reference to the event sink.
    [[nodiscard]] IEventSinkT<EventVariant>& eventSink() const
    {
        return *_eventSink;
    }

    /// @brief Request graceful application exit.
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

/// @brief Default AppCoreContext alias.
using AppCoreContext = AppCoreContextT<DefaultEvents>;

} // namespace corium
