#pragma once

#include "corium/Events.hpp"

namespace corium {

template <typename EventVariant = DefaultEvents>
class IEventSinkT {
public:
    virtual ~IEventSinkT() = default;
    virtual void post(EventVariant event) = 0;
};

using IEventSink = IEventSinkT<DefaultEvents>;

} // namespace corium
