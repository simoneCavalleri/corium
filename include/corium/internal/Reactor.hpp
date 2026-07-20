#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>
#include <variant>
#include <vector>

#include "corium/Events.hpp"
#include "corium/internal/FastDelegate.hpp"
#include "corium/internal/VariantIndex.hpp"

namespace corium {

/// @brief Static Event Reactor providing O(1) direct array indexing and zero type erasure dispatching.
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
class ReactorT {
    static constexpr size_t VariantSize = std::variant_size_v<EventVariant>;

    struct TypeHandlersBase {
        virtual ~TypeHandlersBase() = default;
        virtual void dispatch(const EventVariant& event) = 0;
    };

    template <typename EventType>
    struct TypeHandlers : public TypeHandlersBase {
        std::vector<EventHandlerDelegate<EventType>> handlers;

        void dispatch(const EventVariant& event) override
        {
            const auto& concreteEvent = std::get<EventType>(event);
            for (const auto& handler : handlers) {
                handler.invoke(concreteEvent);
            }
        }
    };

public:
    ReactorT() = default;
    ~ReactorT() = default;

    ReactorT(const ReactorT&) = delete;
    ReactorT& operator=(const ReactorT&) = delete;

    /// @brief Register event handler for concrete event type.
    /// @tparam EventType Event type to handle.
    /// @tparam Handler Callable handler type.
    /// @param handler Callback to invoke when event is dispatched.
    template <typename EventType, typename Handler>
    void registerHandler(Handler&& handler) {
        std::lock_guard<std::mutex> lock(_mutex);

        constexpr size_t eventIndex = variant_index_v<EventType, EventVariant>;

        if (!_handlers[eventIndex]) {
            _handlers[eventIndex] = std::make_unique<TypeHandlers<EventType>>();
        }

        auto* concreteHandlers = static_cast<TypeHandlers<EventType>*>(_handlers[eventIndex].get());
        concreteHandlers->handlers.emplace_back(std::forward<Handler>(handler));
    }

    /// @brief Dispatch event via direct O(1) array lookup (zero lock acquisition once sealed).
    /// @param event Event instance to dispatch.
    void dispatch(const EventVariant& event)
    {
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

    /// @brief Seal reactor handlers for lock-free dispatch during event loop execution.
    void seal()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _sealed = true;
    }

private:
    std::array<std::unique_ptr<TypeHandlersBase>, VariantSize> _handlers;
    std::mutex _mutex;
    bool _sealed = false;
};

/// @brief Default Reactor alias using DefaultEvents.
using Reactor = ReactorT<DefaultEvents>;

} // namespace corium
