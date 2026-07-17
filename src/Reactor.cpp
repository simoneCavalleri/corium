#include "corium/internal/Reactor.h"

namespace corium {

void Reactor::dispatch(const Event& event) {
    size_t currentIndex = event.index();

    if (_sealed) {
        auto it = _indexedHandlers.find(currentIndex);
        if (it != _indexedHandlers.end()) {
            for (const auto& handler : it->second) {
                handler(event);
            }
        }
        return;
    }

    std::vector<std::function<void(const Event&)>> targetedHandlers;

    {
        std::lock_guard<std::mutex> lock(_mutex);

        auto it = _indexedHandlers.find(currentIndex);
        if (it != _indexedHandlers.end()) {
            targetedHandlers = it->second;
        }
    }

    for (const auto& handler : targetedHandlers) {
        handler(event);
    }
}

void Reactor::seal() {
    std::lock_guard<std::mutex> lock(_mutex);
    _sealed = true;
}

} // namespace corium

