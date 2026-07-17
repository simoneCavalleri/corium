#pragma once

#include <functional>
#include <mutex>
#include <utility>
#include <vector>
#include <unordered_map>

#include "corium/Events.h"
#include "corium/internal/VariantIndex.h"

namespace corium {

class Reactor {
public:
    template <typename EventType, typename Handler>
    void registerHandler(Handler&& handler) {
        std::lock_guard<std::mutex> lock(_mutex);

        constexpr size_t eventIndex = variant_index_v<EventType, Event>;

        auto wrapper = [handler = std::forward<Handler>(handler)](const Event& event) {
            if (const auto* concreteEvent = std::get_if<EventType>(&event)) {
                handler(*concreteEvent);
            }
        };

        _indexedHandlers[eventIndex].push_back(std::move(wrapper));
    }

    void dispatch(const Event& event);
    void seal();

private:
    std::unordered_map<size_t, std::vector<std::function<void(const Event&)>>> _indexedHandlers;
    std::mutex _mutex;
    bool _sealed = false;
};

} // namespace corium
