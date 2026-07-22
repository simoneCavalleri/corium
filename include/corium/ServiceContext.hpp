#pragma once

#include "corium/IEventSink.hpp"

namespace corium {

/// @brief Execution context provided to background services.
/// Allows services to post events back to the main event queue thread-safely.
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
struct ServiceContextT {
    IEventSinkT<EventVariant> eventSink;
};

/// @brief Default ServiceContext alias using DefaultEvents.
using ServiceContext = ServiceContextT<DefaultEvents>;

} // namespace corium
