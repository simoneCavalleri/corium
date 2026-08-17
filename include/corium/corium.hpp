#pragma once

/// @file corium.hpp
/// @brief Umbrella header for the Corium C++20 Header-Only Application Runtime.
/// Including this file provides access to Runtime, Application, Events, BackgroundServices, and Policies.

#include "corium/Runtime.hpp"
#include "corium/Application.hpp"
#include "corium/ApplicationContext.hpp"
#include "corium/Service.hpp"
#include "corium/BackgroundService.hpp"
#include "corium/ServiceRegistry.hpp"
#include "corium/Events.hpp"
#include "corium/policies/Policies.hpp"
#include "corium/logging/logging.hpp"
#include "corium/embedded/embedded.hpp"
#include "corium/fsm/fsm.hpp"
#include "corium/async/async.hpp"
#include "corium/wire/wire.hpp"
#include "corium/profiler/profiler.hpp"
#include "corium/safety/safety.hpp"
#include "corium/ipc/ipc.hpp"
