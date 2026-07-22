#pragma once

#include "corium/BackgroundService.hpp"
#include "corium/ServiceContext.hpp"
#include "corium/internal/ServiceManager.hpp"

namespace corium {

/// @brief Compile-time static service registry wrapper.
/// @tparam Services Managed background service types.
template <typename... Services>
struct ServiceRegistryT {
    using ManagerType = ServiceManager<Services...>;
};

/// @brief Default ServiceRegistry alias.
using ServiceRegistry = ServiceRegistryT<>;

} // namespace corium
