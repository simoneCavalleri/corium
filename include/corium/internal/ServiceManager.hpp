#pragma once

#include "corium/ServiceContext.hpp"

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace corium {

/// @brief Static zero-allocation manager for background services.
/// Stores service instances inline in a std::tuple. Zero heap allocations, zero vtables.
/// @tparam Services Concrete background service types.
template <typename... Services>
class ServiceManager {
public:
    ServiceManager() = default;

    /// @brief Initialize all managed services with the service context.
    template <typename ServiceContext>
    void initialize(ServiceContext context)
    {
        std::apply([&context](auto&... service) {
            (([&context](auto& s) {
                if constexpr (requires { s.setContext(context); }) {
                    s.setContext(context);
                }
                if constexpr (requires { s.initialize(); }) {
                    s.initialize();
                }
            }(service)), ...);
        }, _services);
    }

    /// @brief Poll all background services (called periodically on bare-metal or event loop).
    void poll()
    {
        std::apply([](auto&... service) {
            (([&](auto& s) {
                if constexpr (requires { s.poll(); }) {
                    s.poll();
                }
            }(service)), ...);
        }, _services);
    }

    /// @brief Shutdown all managed services cleanly.
    void shutdown() noexcept
    {
        std::apply([](auto&... service) {
            (([&](auto& s) {
                if constexpr (requires { s.shutdown(); }) {
                    s.shutdown();
                }
            }(service)), ...);
        }, _services);
    }

    /// @brief Access reference to a specific service by type.
    template <typename ServiceType>
    [[nodiscard]] ServiceType& get() noexcept
    {
        return std::get<ServiceType>(_services);
    }

    /// @brief Access const reference to a specific service by type.
    template <typename ServiceType>
    [[nodiscard]] const ServiceType& get() const noexcept
    {
        return std::get<ServiceType>(_services);
    }

private:
    std::tuple<Services...> _services;
};

} // namespace corium
