#pragma once

#include <utility>
#include "corium/Events.hpp"

namespace corium {

/// @brief Non-virtual type-erased event sink handle (raw pointer + static function pointer). Zero dynamic allocation, zero vtables/RTTI.
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
class IEventSinkT {
    using PostFn = void (*)(void* sinkPtr, EventVariant event);

public:
    IEventSinkT() = default;

    template <typename ConcreteSink>
    explicit IEventSinkT(ConcreteSink& sink)
        : _sinkPtr(&sink),
          _postFn([](void* ptr, EventVariant evt) {
              reinterpret_cast<ConcreteSink*>(ptr)->post(std::move(evt));
          })
    {}

    /// @brief Post an event into the event sink.
    void post(EventVariant event) const
    {
        if (_postFn && _sinkPtr) {
            _postFn(_sinkPtr, std::move(event));
        }
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
