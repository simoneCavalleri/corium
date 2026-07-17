#pragma once

#include "corium/Events.h"

namespace corium {

class IEventSink {
public:
    virtual ~IEventSink() = default;
    virtual void post(Event event) = 0;
};

} // namespace corium
