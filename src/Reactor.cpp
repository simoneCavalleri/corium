#include "corium/internal/Reactor.h"

namespace corium {

Reactor::Reactor() = default;

void Reactor::dispatch(const Event& event) {
    size_t currentIndex = event.index();
    if (currentIndex >= _handlers.size()) {
        return;
    }

    if (_sealed) {
        if (_handlers[currentIndex]) {
            _handlers[currentIndex]->dispatch(event);
        }
        return;
    }

    TypeHandlersBase* target = nullptr;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_handlers[currentIndex]) {
            target = _handlers[currentIndex].get();
        }
    }

    if (target) {
        target->dispatch(event);
    }
}

void Reactor::seal() {
    std::lock_guard<std::mutex> lock(_mutex);
    _sealed = true;
}

} // namespace corium
