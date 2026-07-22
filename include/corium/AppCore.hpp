#pragma once

#include "corium/AppCoreContext.hpp"
#include "corium/IEventSink.hpp"

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
};

} // namespace corium
