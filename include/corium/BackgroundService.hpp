#pragma once

#include "corium/IEventSink.hpp"
#include "corium/ServiceContext.hpp"

#include <stop_token>
#include <utility>

namespace corium {

class IBackgroundService {
public:
    virtual ~IBackgroundService() = default;
    virtual void run(std::stop_token stopToken) = 0;
};

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
