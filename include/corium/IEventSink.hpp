#pragma once

#include <utility>
#include "corium/Events.hpp"
#include "corium/policies/QueuePolicies.hpp"

namespace corium {

/// @brief Non-virtual type-erased event sink handle (raw pointer + static function pointer). Zero dynamic allocation, zero vtables/RTTI.
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
class IEventSinkT {
    using PostFn = void (*)(void* sinkPtr, EventVariant event, EventPriority priority);

public:
    IEventSinkT() = default;

    template <typename ConcreteSink>
    explicit IEventSinkT(ConcreteSink& sink)
        : _sinkPtr(&sink),
          _postFn([](void* ptr, EventVariant evt, EventPriority prio) {
              reinterpret_cast<ConcreteSink*>(ptr)->post(std::move(evt), prio);
          })
    {}

    /// @brief Post an event into the event sink with priority.
    void post(EventVariant event, EventPriority priority = EventPriority::Normal) const
    {
        if (_postFn && _sinkPtr) {
            _postFn(_sinkPtr, std::move(event), priority);
        }
    }

    /// @brief Convenience helper for posting high-priority events (e.g. from ISR handlers).
    void postHighPriority(EventVariant event) const
    {
        post(std::move(event), EventPriority::High);
    }

    explicit operator bool() const noexcept
    {
        return _sinkPtr != nullptr && _postFn != nullptr;
    }

private:
    void* _sinkPtr = nullptr;
    PostFn _postFn = nullptr;
};

/// @brief Default IEventSink alias using DefaultEvents.
using IEventSink = IEventSinkT<DefaultEvents>;

} // namespace corium
