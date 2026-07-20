#pragma once

#include "corium/IEventSink.hpp"

namespace corium {

/// @brief Execution context provided to background services.
/// Allows services to post events back to the main event queue thread-safely.
struct ServiceContext {
    IEventSink& eventSink;
};

} // namespace corium
