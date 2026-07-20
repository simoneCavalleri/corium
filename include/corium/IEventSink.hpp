#pragma once

#include "corium/Events.hpp"

namespace corium {

/// @brief Abstract interface for pushing events into the runtime event queue.
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
class IEventSinkT {
public:
    virtual ~IEventSinkT() = default;

    /// @brief Post an event into the event sink (thread-safe, lock-free).
    /// @param event Event instance to post into the queue.
    virtual void post(EventVariant event) = 0;
};

/// @brief Default IEventSink alias using DefaultEvents.
using IEventSink = IEventSinkT<DefaultEvents>;

} // namespace corium
