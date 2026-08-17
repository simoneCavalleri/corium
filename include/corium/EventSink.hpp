#pragma once

#include <type_traits>
#include <utility>
#include "corium/Events.hpp"
#include "corium/policies/QueuePolicies.hpp"

namespace corium {

/// @ingroup core
/// @brief Non-virtual type-erased event sink handle (raw pointer + static function pointer).
/// Zero dynamic heap allocations, zero vtables/RTTI.
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
class BasicEventSink {
    using PostFn = void (*)(void* sinkPtr, EventVariant&& event, EventPriority priority);

public:
    using EventVariantType = EventVariant;

    BasicEventSink() = default;

    BasicEventSink(const BasicEventSink&) = default;
    BasicEventSink& operator=(const BasicEventSink&) = default;
    BasicEventSink(BasicEventSink&&) noexcept = default;
    BasicEventSink& operator=(BasicEventSink&&) noexcept = default;

    template <typename ConcreteSink>
        requires (!std::is_same_v<std::decay_t<ConcreteSink>, BasicEventSink>)
    explicit BasicEventSink(ConcreteSink& sink)
        : _sinkPtr(&sink),
          _postFn([](void* ptr, EventVariant&& evt, EventPriority prio) {
              static_cast<ConcreteSink*>(ptr)->post(std::move(evt), prio);
          })
    {}

    /// @brief Post an event into the event sink with priority (rvalue overload).
    void post(EventVariant&& event, EventPriority priority = EventPriority::Normal) const
    {
        if (_postFn && _sinkPtr) {
            _postFn(_sinkPtr, std::move(event), priority);
        }
    }

    /// @brief Post an event into the event sink with priority (const lvalue overload).
    void post(const EventVariant& event, EventPriority priority = EventPriority::Normal) const
    {
        if (_postFn && _sinkPtr) {
            EventVariant copy = event;
            _postFn(_sinkPtr, std::move(copy), priority);
        }
    }

    /// @brief Convenience helper for posting high-priority events (e.g. from ISR handlers).
    template <typename Event>
    void postHighPriority(Event&& event) const
    {
        post(EventVariant(std::forward<Event>(event)), EventPriority::High);
    }

    explicit operator bool() const noexcept
    {
        return _sinkPtr != nullptr && _postFn != nullptr;
    }

private:
    void* _sinkPtr = nullptr;
    PostFn _postFn = nullptr;
};

/// @brief Default EventSink alias using DefaultEvents.
using EventSink = BasicEventSink<DefaultEvents>;

/// @brief Templated EventSink alias for custom event variant list.
template <typename EventVariant = DefaultEvents>
using EventSinkT = BasicEventSink<EventVariant>;

} // namespace corium
