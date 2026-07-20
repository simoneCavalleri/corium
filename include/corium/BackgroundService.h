#pragma once

#include "corium/IBackgroundService.h"
#include "corium/IEventSink.h"
#include "corium/ServiceContext.h"

#include <utility>

namespace corium {

class BackgroundService : public IBackgroundService {
public:
    explicit BackgroundService(const ServiceContext& context)
        : _context(context)
    {}

    ~BackgroundService() override = default;

protected:
    [[nodiscard]] ServiceContext& context() noexcept { return _context; }
    [[nodiscard]] const ServiceContext& context() const noexcept { return _context; }

    [[nodiscard]] IEventSink& events() noexcept { return _context.eventSink; }
    [[nodiscard]] IEventSink& eventSink() noexcept { return _context.eventSink; }

    template <typename EventType>
    void postEvent(EventType&& event)
    {
        _context.eventSink.post(std::forward<EventType>(event));
    }

private:
    ServiceContext _context;
};

} // namespace corium
