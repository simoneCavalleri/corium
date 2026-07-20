#pragma once

#include "corium/AppCoreContext.hpp"
#include "corium/ServiceRegistry.hpp"
#include "corium/EventBus.hpp"
#include "corium/IEventSink.hpp"

#include <cstddef>

namespace corium {

template <
    typename EventVariant,
    typename QueuePolicy,
    typename SignalPolicy,
    typename DispatchPolicy
>
class BasicRuntime;

template <typename EventVariant = DefaultEvents>
class AppCoreT {
public:
    virtual ~AppCoreT() = default;

    AppCoreT(const AppCoreT&) = delete;
    AppCoreT& operator=(const AppCoreT&) = delete;

    AppCoreT(AppCoreT&&) = delete;
    AppCoreT& operator=(AppCoreT&&) = delete;

protected:
    AppCoreT() = default;

    EventBusBaseT<EventVariant>& events()
    {
        return _context.events();
    }

    IEventSinkT<EventVariant>& eventSink()
    {
        return _context.eventSink();
    }

    void requestQuit()
    {
        _context.requestQuit();
    }

private:
    void configureServices(ServiceRegistry& registry)
    {
        if (_state >= State::Configured) {
            return;
        }

        onConfigureServices(registry);
        _state = State::Configured;
    }

    void registerHandlers()
    {
        if (_state >= State::Ready) {
            return;
        }

        onRegisterHandlers();
        _state = State::Ready;
    }

    void initialize()
    {
        if (_state >= State::Running) {
            return;
        }

        onInitialize();
        _state = State::Running;
    }

    void shutdown()
    {
        if (_state == State::Shutdown) {
            return;
        }

        onShutdown();
        _state = State::Shutdown;
    }

private:
    virtual void onConfigureServices(ServiceRegistry& registry) {};
    virtual void onRegisterHandlers() {};
    virtual void onInitialize() = 0;
    virtual void onShutdown() {};

private:
    enum class State {
        Created,
        Configured,
        Ready,
        Running,
        Shutdown
    };

    AppCoreContextT<EventVariant> _context;

    State _state = State::Created;

    void setContext(AppCoreContextT<EventVariant> context)
    {
        _context = context;
    }

    template <typename EV, typename QP, typename SP, typename DP>
    friend class BasicRuntime;
};

using AppCore = AppCoreT<DefaultEvents>;

} // namespace corium
