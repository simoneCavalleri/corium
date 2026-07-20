#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>
#include <variant>
#include <vector>

#include "corium/Events.h"
#include "corium/internal/FastDelegate.h"
#include "corium/internal/VariantIndex.h"

namespace corium {

class Reactor {
    static constexpr size_t VariantSize = std::variant_size_v<Event>;

    struct TypeHandlersBase {
        virtual ~TypeHandlersBase() = default;
        virtual void dispatch(const Event& event) = 0;
    };

    template <typename EventType>
    struct TypeHandlers : public TypeHandlersBase {
        std::vector<EventHandlerDelegate<EventType>> handlers;

        void dispatch(const Event& event) override
        {
            const auto& concreteEvent = std::get<EventType>(event);
            for (const auto& handler : handlers) {
                handler.invoke(concreteEvent);
            }
        }
    };

public:
    Reactor();
    ~Reactor() = default;

    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;

    template <typename EventType, typename Handler>
    void registerHandler(Handler&& handler) {
        std::lock_guard<std::mutex> lock(_mutex);

        constexpr size_t eventIndex = variant_index_v<EventType, Event>;

        if (!_handlers[eventIndex]) {
            _handlers[eventIndex] = std::make_unique<TypeHandlers<EventType>>();
        }

        auto* concreteHandlers = static_cast<TypeHandlers<EventType>*>(_handlers[eventIndex].get());
        concreteHandlers->handlers.emplace_back(std::forward<Handler>(handler));
    }

    void dispatch(const Event& event);
    void seal();

private:
    std::array<std::unique_ptr<TypeHandlersBase>, VariantSize> _handlers;
    std::mutex _mutex;
    bool _sealed = false;
};

} // namespace corium
