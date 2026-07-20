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

/// @brief Abstract base class for applications managed by Corium Runtime.
/// Subclass AppCore to register event handlers, configure services, and manage application lifecycle.
/// @tparam EventVariant The variant type list of supported events.
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

    /// @brief Register event handler with automatic event type deduction from callable signature.
    /// @tparam Handler Callable handler type (lambda or function pointer).
    /// @param handler Callback to invoke when event occurs.
    template <typename Handler>
    void on(Handler&& handler)
    {
        _context.events().registerHandler(std::forward<Handler>(handler));
    }

    /// @brief Register event handler with automatic event type deduction (alias for on).
    /// @tparam Handler Callable handler type.
    /// @param handler Callback to invoke when event occurs.
    template <typename Handler>
    void handle(Handler&& handler)
    {
        _context.events().registerHandler(std::forward<Handler>(handler));
    }

    /// @brief Access event bus reference.
    [[nodiscard]] EventBusBaseT<EventVariant>& events()
    {
        return _context.events();
    }

    /// @brief Access event sink reference.
    [[nodiscard]] IEventSinkT<EventVariant>& eventSink()
    {
        return _context.eventSink();
    }

    /// @brief Request graceful application shutdown.
    void requestQuit()
    {
        _context.requestQuit();
    }

private:
    void configureServices(ServiceRegistryT<EventVariant>& registry)
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
    /// @brief Override to configure background services.
    /// @param registry Service registry to register background services.
    virtual void onConfigureServices(ServiceRegistryT<EventVariant>& registry) {};

    /// @brief Override to wire up event handlers using events().registerHandler<T>().
    virtual void onRegisterHandlers() {};

    /// @brief Override to execute custom application initialization logic.
    virtual void onInitialize() = 0;

    /// @brief Override to execute application cleanup logic on shutdown.
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

/// @brief Default AppCore alias using DefaultEvents.
using AppCore = AppCoreT<DefaultEvents>;

} // namespace corium
