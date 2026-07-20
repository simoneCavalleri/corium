#pragma once

#include "corium/BackgroundService.hpp"
#include "corium/ServiceContext.hpp"

#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace corium {

class ServiceManager;

/// @brief Service registry for configuring background services before runtime initialization.
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
class ServiceRegistryT {
public:
    using ServiceFactory =
        std::function<std::unique_ptr<IBackgroundService>(ServiceContextT<EventVariant>&)>;

    /// @brief Add a custom service factory lambda.
    /// @param factory Service factory lambda.
    void addFactory(ServiceFactory factory)
    {
        _serviceFactories.push_back(std::move(factory));
    }

    /// @brief Register a background service type with constructor arguments.
    /// @tparam ServiceType Type of the background service deriving from IBackgroundService.
    /// @tparam Args Constructor argument types.
    /// @param args Arguments passed to ServiceType constructor.
    template <typename ServiceType, typename... Args>
    void addService(Args&&... args)
    {
        static_assert(
            std::is_base_of_v<IBackgroundService, ServiceType>,
            "ServiceType must derive from IBackgroundService"
        );

        auto capturedArgs = std::make_tuple(std::forward<Args>(args)...);

        _serviceFactories.emplace_back(
            [capturedArgs = std::move(capturedArgs)]
            (ServiceContextT<EventVariant>& context) mutable -> std::unique_ptr<IBackgroundService>
            {
                return std::apply(
                    [&context](auto&&... unpackedArgs) -> std::unique_ptr<IBackgroundService>
                    {
                        if constexpr (std::is_constructible_v<ServiceType, ServiceContextT<EventVariant>&, decltype(unpackedArgs)...>)
                        {
                            return std::make_unique<ServiceType>(
                                context,
                                std::forward<decltype(unpackedArgs)>(unpackedArgs)...
                            );
                        }
                        else if constexpr (std::is_constructible_v<ServiceType, const ServiceContextT<EventVariant>&, decltype(unpackedArgs)...>)
                        {
                            return std::make_unique<ServiceType>(
                                context,
                                std::forward<decltype(unpackedArgs)>(unpackedArgs)...
                            );
                        }
                        else if constexpr (std::is_constructible_v<ServiceType, decltype(unpackedArgs)...>)
                        {
                            return std::make_unique<ServiceType>(
                                std::forward<decltype(unpackedArgs)>(unpackedArgs)...
                            );
                        }
                        else
                        {
                            static_assert(
                                std::is_constructible_v<ServiceType, ServiceContextT<EventVariant>&, decltype(unpackedArgs)...>,
                                "ServiceType cannot be constructed with the provided arguments (with or without ServiceContext)."
                            );
                        }
                    },
                    std::move(capturedArgs)
                );
            }
        );
    }

private:
    friend class ServiceManager;

    std::vector<ServiceFactory> _serviceFactories;
};

/// @brief Default ServiceRegistry alias using DefaultEvents.
using ServiceRegistry = ServiceRegistryT<DefaultEvents>;

} // namespace corium
