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

class ServiceRegistry {
public:
    using ServiceFactory =
        std::function<std::unique_ptr<IBackgroundService>(ServiceContext&)>;

    void addFactory(ServiceFactory factory)
    {
        _serviceFactories.push_back(std::move(factory));
    }

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
            (ServiceContext& context) mutable -> std::unique_ptr<IBackgroundService>
            {
                return std::apply(
                    [&context](auto&&... unpackedArgs) -> std::unique_ptr<IBackgroundService>
                    {
                        if constexpr (std::is_constructible_v<ServiceType, ServiceContext&, decltype(unpackedArgs)...>)
                        {
                            return std::make_unique<ServiceType>(
                                context,
                                std::forward<decltype(unpackedArgs)>(unpackedArgs)...
                            );
                        }
                        else if constexpr (std::is_constructible_v<ServiceType, const ServiceContext&, decltype(unpackedArgs)...>)
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
                                std::is_constructible_v<ServiceType, ServiceContext&, decltype(unpackedArgs)...>,
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

} // namespace corium
