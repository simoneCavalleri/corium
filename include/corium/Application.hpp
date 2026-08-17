#pragma once

#include "corium/ApplicationContext.hpp"
#include "corium/EventBus.hpp"
#include "corium/EventSink.hpp"
#include "corium/ServiceRegistry.hpp"

#include <utility>

namespace corium {

// Forward declaration of BasicRuntime for friendship
template <
    typename EventVariant,
    typename QueuePolicy,
    typename SignalPolicy,
    typename StoragePolicy,
    typename OverflowPolicy,
    typename TimerStoragePolicy,
    typename ProfilerPolicy
>
class BasicRuntime;

/// @brief Static CRTP base class for applications managed by Corium Runtime.
/// Subclass Application<Derived> or Application<Derived, EventBusType, MaxServices> for zero-vtable compile-time static dispatch.
/// All framework operations and handlers are protected for clean encapsulation within the derived application.
/// @tparam Derived Subclass type implementing lifecycle hooks (onRegisterHandlers, onInitialize, onShutdown, onConfigureServices).
/// @tparam EventBusType EventBus type used by the runtime (defaults to EventBus).
/// @tparam MaxServices Maximum number of background services that can be registered (defaults to 8).
template <typename Derived, typename EventBusType = EventBus, std::size_t MaxServices = 8>
class Application {
public:
    using EventVariant = typename EventBusType::EventVariant;
    using ServiceRegistryType = BasicServiceRegistry<MaxServices, EventVariant>;

    Application() = default;
    ~Application() = default;

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

protected:
    /// @brief Register event handler with automatic event type deduction from callable signature.
    template <typename Handler>
    bool on(Handler&& handler)
    {
        return _context.registerHandler(std::forward<Handler>(handler));
    }

    /// @brief Access event sink handle.
    [[nodiscard]] EventSinkT<EventVariant> eventSink()
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

    /// @brief Access reference to the internal ServiceRegistry.
    [[nodiscard]] ServiceRegistryType& services() noexcept
    {
        return _serviceRegistry;
    }

    /// @brief Access const reference to the internal ServiceRegistry.
    [[nodiscard]] const ServiceRegistryType& services() const noexcept
    {
        return _serviceRegistry;
    }

    /// @brief Retrieve a registered service instance by concrete type.
    /// @tparam ServiceType Type of the target background service.
    /// @return Pointer to registered ServiceType instance, or nullptr if not registered.
    template <typename ServiceType>
    [[nodiscard]] ServiceType* getService() noexcept
    {
        return _serviceRegistry.template getService<ServiceType>();
    }

    /// @brief Retrieve a registered service instance by concrete type (const overload).
    /// @tparam ServiceType Type of the target background service.
    /// @return Pointer to registered ServiceType instance, or nullptr if not registered.
    template <typename ServiceType>
    [[nodiscard]] const ServiceType* getService() const noexcept
    {
        return _serviceRegistry.template getService<ServiceType>();
    }

private:
    template <typename, typename, typename, typename, typename, typename, typename>
    friend class BasicRuntime;

    template <typename Registry>
    void configureServices(Registry& registry)
    {
        if constexpr (requires(Derived& d, Registry& r) { d.onConfigureServices(r); }) {
            static_cast<Derived*>(this)->onConfigureServices(registry);
        }
    }

    void initializeServices(EventSinkT<EventVariant> sink)
    {
        configureServices(_serviceRegistry);
        _serviceRegistry.initialize(BasicServiceContext<EventVariant>{sink});
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

    void setContext(ApplicationContext<EventBusType> context)
    {
        _context = context;
        if constexpr (requires(Derived& d, ApplicationContext<EventBusType> c) { d.onSetContext(c); }) {
            static_cast<Derived*>(this)->onSetContext(context);
        }
    }

    ApplicationContext<EventBusType> _context;
    ServiceRegistryType _serviceRegistry;
};

} // namespace corium
