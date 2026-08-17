#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "corium/Events.hpp"
#include "corium/internal/CallableTraits.hpp"
#include "corium/internal/FastDelegate.hpp"
#include "corium/internal/Panic.hpp"
#include "corium/internal/VariantIndex.hpp"
#include "corium/policies/StoragePolicies.hpp"

namespace corium {
namespace internal {

/// @brief Fixed-capacity stack-allocated list of event handlers for a single event type.
/// @tparam EventType Event type handled.
/// @tparam MaxHandlers Maximum number of handlers allowed for this event type.
/// @tparam InlineSize Max payload size in bytes for handler inline storage.
template <typename EventType, size_t MaxHandlers, size_t InlineSize>
class FixedHandlerList {
public:
    FixedHandlerList() = default;

    template <typename Handler>
    bool registerHandler(Handler&& handler) {
        if (_count >= MaxHandlers) {
            return false;
        }
        _handlers[_count] = EventHandlerDelegate<EventType, InlineSize>(std::forward<Handler>(handler));
        _count++;
        return true;
    }

    void dispatch(const EventType& event) const {
        for (size_t i = 0; i < _count; ++i) {
            _handlers[i].invoke(event);
        }
    }

    [[nodiscard]] size_t size() const noexcept {
        return _count;
    }

private:
    std::array<EventHandlerDelegate<EventType, InlineSize>, MaxHandlers> _handlers{};
    size_t _count = 0;
};

} // namespace internal

/// @brief Primary template declaration for BasicReactor.
template <typename EventVariant = DefaultEvents, typename StoragePolicy = DefaultStoragePolicy>
class BasicReactor;

/// @brief Static Event Reactor providing compile-time zero-heap dispatching.
/// @tparam Events Supported event types in std::variant.
/// @tparam StoragePolicy Configuration policy for handler capacity and inline storage size.
template <typename... Events, typename StoragePolicy>
class BasicReactor<std::variant<Events...>, StoragePolicy> {
    static constexpr size_t MaxHandlersPerEvent = StoragePolicy::max_handlers_per_event;
    static constexpr size_t InlineSize = StoragePolicy::inline_storage_size;

public:
    using EventVariant = std::variant<Events...>;
    using StoragePolicyType = StoragePolicy;

    BasicReactor() = default;
    ~BasicReactor() = default;

    BasicReactor(const BasicReactor&) = delete;
    BasicReactor& operator=(const BasicReactor&) = delete;

    BasicReactor(BasicReactor&&) noexcept = default;
    BasicReactor& operator=(BasicReactor&&) noexcept = default;

    /// @brief Register event handler for concrete event type with explicit type parameter.
    /// @tparam EventType Event type to handle.
    /// @tparam Handler Callable handler type.
    /// @param handler Callback to invoke when event is dispatched.
    /// @note Must be called before runtime.initialize() seals the reactor.
    template <typename EventType, typename Handler>
    bool registerHandler(Handler&& handler) {
        CORIUM_ASSERT(!_sealed, "registerHandler() called after reactor was sealed by runtime.initialize(). Move handler registration into onRegisterHandlers().");
        static_assert(has_variant_type_v<EventType, EventVariant>, "EventType is not part of EventVariant!");
        auto& list = std::get<internal::FixedHandlerList<EventType, MaxHandlersPerEvent, InlineSize>>(_handlers);
        return list.registerHandler(std::forward<Handler>(handler));
    }

    /// @brief Register event handler with automatic event type deduction from handler parameter signature.
    /// @tparam Handler Callable handler type (lambda, function pointer, or functor).
    /// @param handler Callback to invoke when event is dispatched.
    /// @note Must be called before runtime.initialize() seals the reactor.
    template <typename Handler>
    bool registerHandler(Handler&& handler) {
        CORIUM_ASSERT(!_sealed, "registerHandler() called after reactor was sealed by runtime.initialize(). Move handler registration into onRegisterHandlers().");
        using EventType = callable_event_type_t<Handler>;
        return registerHandler<EventType>(std::forward<Handler>(handler));
    }

    /// @brief Dispatch event via std::visit to concrete static handler array.
    /// @param event Event instance to dispatch.
    void dispatch(const EventVariant& event) const {
        std::visit([this](const auto& concreteEvent) {
            using EventType = std::decay_t<decltype(concreteEvent)>;
            const auto& list = std::get<internal::FixedHandlerList<EventType, MaxHandlersPerEvent, InlineSize>>(_handlers);
            list.dispatch(concreteEvent);
        }, event);
    }

    /// @brief Seal reactor handlers.
    void seal() noexcept {
        _sealed = true;
    }

    /// @brief Check if reactor has been sealed.
    [[nodiscard]] bool sealed() const noexcept {
        return _sealed;
    }

private:
    std::tuple<internal::FixedHandlerList<Events, MaxHandlersPerEvent, InlineSize>...> _handlers{};
    bool _sealed = false;
};

/// @brief Default Reactor alias using DefaultEvents and DefaultStoragePolicy.
using Reactor = BasicReactor<DefaultEvents>;

/// @brief Templated Reactor alias for custom event variant and storage policy.
template <typename EventVariant = DefaultEvents, typename StoragePolicy = DefaultStoragePolicy>
using ReactorT = BasicReactor<EventVariant, StoragePolicy>;

} // namespace corium
