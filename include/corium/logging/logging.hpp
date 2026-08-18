/**
 * @file logging.hpp
 * @ingroup logging
 * @brief Umbrella header for the zero-heap structured logging framework.
 */

#pragma once

#include "corium/logging/LogLevel.hpp"
#include "corium/logging/LogEvent.hpp"
#include "corium/logging/sinks/ConsoleLogSink.hpp"
#include "corium/logging/sinks/FileLogSink.hpp"
#include "corium/logging/sinks/JsonLogSink.hpp"
#include "corium/logging/sinks/NullLogSink.hpp"
#include "corium/logging/Logger.hpp"
#include "corium/logging/LogBackgroundService.hpp"
