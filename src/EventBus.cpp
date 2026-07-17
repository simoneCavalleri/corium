#include "corium/EventBus.h"

namespace corium {

void EventBus::post(Event event) {
    _eventQueue.pushEvent(std::move(event));
}

bool EventBus::processOne() {
    auto eventOpt = _eventQueue.tryPopEvent();
    if (!eventOpt) {
        return false;
    }
    _reactor.dispatch(*eventOpt);
    return true;
}

void EventBus::seal() {
    _reactor.seal();
}

} // namespace corium

