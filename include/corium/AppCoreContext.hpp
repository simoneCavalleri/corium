#pragma once

#include "corium/IEventSink.hpp"
#include "corium/policies/SignalPolicies.hpp"

namespace corium {

/// @brief Context object passed to AppCore providing event bus access and quit requests.
/// @tparam EventBusType Concrete event bus type used by the runtime.
template <typename EventBusType>
class AppCoreContextT {
public:
    using EventVariant = typename EventBusType::EventVariant;

    AppCoreContextT() = default;

    AppCoreContextT(EventBusType& events, StaticCallback quitCallback)
        : _events(&events), _quitCallback(quitCallback)
    {
    }

    /// @brief Access reference to the event bus.
    [[nodiscard]] EventBusType& events() const
    {
        return *_events;
    }

    /// @brief Access event sink handle.
    [[nodiscard]] IEventSinkT<EventVariant> eventSink() const
    {
        return _events->sink();
    }

    /// @brief Request graceful application exit.
    void requestQuit() const
    {
        if (_quitCallback) {
            _quitCallback();
        }
    }

    explicit operator bool() const noexcept
    {
        return _events != nullptr;
    }

private:
    EventBusType* _events = nullptr;
    StaticCallback _quitCallback;
};

} // namespace corium
