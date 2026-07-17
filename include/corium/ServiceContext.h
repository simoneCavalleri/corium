#pragma once

#include "corium/IEventSink.h"

namespace corium {

struct ServiceContext {
    IEventSink& eventSink;
};

} // namespace corium

