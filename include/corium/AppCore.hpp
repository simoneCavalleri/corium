#pragma once

#include "corium/AppCoreContext.hpp"
#include "corium/IEventSink.hpp"
#include "corium/ServiceRegistry.hpp"

#include <utility>

namespace corium {

/// @brief Static CRTP base class for applications managed by Corium Runtime.
/// Subclass AppCoreT<Derived, EventBusType> for zero-vtable compile-time static dispatch.
/// @tparam Derived Subclass type implementing lifecycle hooks (onRegisterHandlers, onInitialize, onShutdown, onConfigureServices).
/// @tparam EventBusType EventBus type used by the runtime.
template <typename Derived, typename EventBusType>
class AppCoreT {
public:
    using EventVariant = typename EventBusType::EventVariant;
    using ServiceRegistryType = ServiceRegistryT<8, EventVariant>;

    AppCoreT() = default;

    AppCoreT(const AppCoreT&) = delete;
    AppCoreT& operator=(const AppCoreT&) = delete;

    AppCoreT(AppCoreT&&) = delete;
    AppCoreT& operator=(AppCoreT&&) = delete;

protected:
    /// @brief Register event handler with automatic event type deduction from callable signature.
    template <typename Handler>
    bool on(Handler&& handler)
    {
        return _context.events().registerHandler(std::forward<Handler>(handler));
    }

    /// @brief Register event handler with automatic event type deduction (alias for on).
    template <typename Handler>
    bool handle(Handler&& handler)
    {
        return _context.events().registerHandler(std::forward<Handler>(handler));
    }

    /// @brief Access event bus reference.
    [[nodiscard]] EventBusType& events()
    {
        return _context.events();
    }

    /// @brief Access event sink handle.
    [[nodiscard]] IEventSinkT<EventVariant> eventSink()
    {
        return _context.eventSink();
    }

    /// @brief Schedule a single-shot delayed event.
    template <typename Rep, typename Period>
    TimerId postDelayed(EventVariant event, const std::chrono::duration<Rep, Period>& delay, EventPriority priority = EventPriority::Normal)
    {
        return _context.scheduleDelayed(std::move(event), std::chrono::duration_cast<std::chrono::microseconds>(delay), priority);
    }

    /// @brief Schedule a recurring periodic event.
    template <typename Rep, typename Period>
    TimerId postPeriodic(EventVariant event, const std::chrono::duration<Rep, Period>& interval, EventPriority priority = EventPriority::Normal)
    {
        return _context.schedulePeriodic(std::move(event), std::chrono::duration_cast<std::chrono::microseconds>(interval), priority);
    }

    /// @brief Cancel an active timer handle.
    bool cancelTimer(TimerId id)
    {
        return _context.cancelTimer(id);
    }

    /// @brief Request graceful application shutdown.
    void requestQuit()
    {
        _context.requestQuit();
    }

public:
    template <typename Registry>
    void configureServices(Registry& registry)
    {
        if constexpr (requires(Derived& d, Registry& r) { d.onConfigureServices(r); }) {
            static_cast<Derived*>(this)->onConfigureServices(registry);
        }
    }

    void initializeServices(IEventSinkT<EventVariant> sink)
    {
        configureServices(_serviceRegistry);
        _serviceRegistry.initialize(ServiceContextT<EventVariant>{sink});
    }

    void shutdownServices() noexcept
    {
        _serviceRegistry.shutdown();
    }

    void registerHandlers()
    {
        if constexpr (requires(Derived& d) { d.onRegisterHandlers(); }) {
            static_cast<Derived*>(this)->onRegisterHandlers();
        }
    }

    void initialize()
    {
        if constexpr (requires(Derived& d) { d.onInitialize(); }) {
            static_cast<Derived*>(this)->onInitialize();
        }
    }

    void shutdown()
    {
        if constexpr (requires(Derived& d) { d.onShutdown(); }) {
            static_cast<Derived*>(this)->onShutdown();
        }
    }

    void setContext(AppCoreContextT<EventBusType> context)
    {
        _context = context;
    }

private:
    AppCoreContextT<EventBusType> _context;
    ServiceRegistryType _serviceRegistry;
};

} // namespace corium
