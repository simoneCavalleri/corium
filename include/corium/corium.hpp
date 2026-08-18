/**
 * @file corium.hpp
 * @ingroup core
 * @brief Master umbrella header for the entire Corium runtime framework.
 */

#pragma once

// IWYU pragma: begin_exports
#include "corium/Runtime.hpp"
#include "corium/Application.hpp"
#include "corium/ApplicationContext.hpp"
#include "corium/MpscRingBuffer.hpp"
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
// IWYU pragma: end_exports
