/**
 * @file async.hpp
 * @ingroup async
 * @brief Umbrella header for C++20 coroutine primitives.
 */

#pragma once

#include "corium/async/FramePool.hpp"        // IWYU pragma: export
#include "corium/async/Task.hpp"             // IWYU pragma: export
#include "corium/async/Delay.hpp"            // IWYU pragma: export
#include "corium/async/CancellationToken.hpp" // IWYU pragma: export
#include "corium/async/WhenAll.hpp"          // IWYU pragma: export
#include "corium/async/WhenAny.hpp"          // IWYU pragma: export
#include "corium/async/Generator.hpp"        // IWYU pragma: export
#include "corium/async/AsyncEvent.hpp"       // IWYU pragma: export
#include "corium/async/Channel.hpp"          // IWYU pragma: export
#include "corium/async/Semaphore.hpp"        // IWYU pragma: export
