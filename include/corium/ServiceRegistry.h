#pragma once

#include "corium/BackgroundService.h"
#include "corium/IBackgroundService.h"
#include "corium/ServiceContext.h"

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
                        return std::make_unique<ServiceType>(
                            context,
                            std::forward<decltype(unpackedArgs)>(unpackedArgs)...
                        );
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
