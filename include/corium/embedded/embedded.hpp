/**
 * @file embedded.hpp
 * @ingroup embedded
 * @brief Umbrella header for embedded and RTOS integration primitives.
 */

#pragma once

#include "corium/embedded/InterruptLock.hpp" // IWYU pragma: export
#include "corium/embedded/IsrSink.hpp"       // IWYU pragma: export
#include "corium/embedded/FreeRtos.hpp"      // IWYU pragma: export
#include "corium/embedded/CanAdapter.hpp"    // IWYU pragma: export
#include "corium/embedded/DmaUartAdapter.hpp" // IWYU pragma: export
#include "corium/embedded/SpiAdapter.hpp"    // IWYU pragma: export
#include "corium/embedded/I2cAdapter.hpp"    // IWYU pragma: export
