#pragma once

#include <utility>
#include "corium/EventSink.hpp"
#include "corium/policies/QueuePolicies.hpp"

namespace corium::embedded {

/// @brief Lightweight, zero-overhead wrapper around EventSink explicitly tailored for Hardware ISR handlers.
/// Guarantees zero heap allocation, lock-free execution, and noexcept exception safety.
/// @tparam EventSinkType Target underlying event sink type.
template <typename EventSinkType>
class IsrEventSink {
public:
    constexpr IsrEventSink() noexcept = default;

    constexpr explicit IsrEventSink(EventSinkType sink) noexcept
        : _sink(sink)
    {}

    /// @brief Post an event safely from hardware ISR context.
    /// @tparam Event Concrete event type.
    /// @param event Event instance to post.
    /// @param priority Priority level (High, Normal, Low).
    template <typename Event>
    void postFromIsr(Event&& event, EventPriority priority = EventPriority::Normal) noexcept
    {
        _sink.post(std::forward<Event>(event), priority);
    }

    /// @brief Post a high-priority emergency or hardware interrupt event from ISR context.
    /// @tparam Event Concrete event type.
    /// @param event Event instance to post.
    template <typename Event>
    void postHighPriorityFromIsr(Event&& event) noexcept
    {
        _sink.postHighPriority(std::forward<Event>(event));
    }

    /// @brief Post an event from ISR context.
    template <typename Event>
    void tryPostFromIsr(Event&& event, EventPriority priority = EventPriority::Normal) noexcept
    {
        _sink.post(std::forward<Event>(event), priority);
    }

    /// @brief Access underlying EventSink handle.
    [[nodiscard]] constexpr EventSinkType& sink() noexcept
    {
        return _sink;
    }

    [[nodiscard]] constexpr const EventSinkType& sink() const noexcept
    {
        return _sink;
    }

private:
    EventSinkType _sink{};
};

/// @brief Helper function to construct an IsrEventSink from an EventSink handle.
template <typename EventSinkType>
[[nodiscard]] constexpr auto makeIsrSink(EventSinkType sink) noexcept
{
    return IsrEventSink<EventSinkType>(sink);
}

} // namespace corium::embedded
