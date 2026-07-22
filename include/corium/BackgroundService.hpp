#pragma once

#include "corium/IEventSink.hpp"
#include "corium/ServiceContext.hpp"
#include <utility>

namespace corium {

/// @brief Non-allocating base class for background services storing ServiceContext.
/// @tparam EventVariant Supported event variant type list.
template <typename EventVariant = DefaultEvents>
class BackgroundService {
public:
    BackgroundService() = default;

    explicit BackgroundService(ServiceContextT<EventVariant> context)
        : _context(context)
    {}

    void setContext(ServiceContextT<EventVariant> context) noexcept
    {
        _context = context;
    }

protected:
    [[nodiscard]] ServiceContextT<EventVariant>& context() noexcept { return _context; }
    [[nodiscard]] const ServiceContextT<EventVariant>& context() const noexcept { return _context; }

    [[nodiscard]] IEventSinkT<EventVariant> events() const noexcept { return _context.eventSink; }
    [[nodiscard]] IEventSinkT<EventVariant> eventSink() const noexcept { return _context.eventSink; }

    template <typename EventType>
    void postEvent(EventType&& event) const
    {
        _context.eventSink.post(std::forward<EventType>(event));
    }

private:
    ServiceContextT<EventVariant> _context;
};

} // namespace corium
