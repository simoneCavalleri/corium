#pragma once

#include "corium/IEventSink.hpp"

namespace corium {

struct ServiceContext {
    IEventSink& eventSink;
};

} // namespace corium

