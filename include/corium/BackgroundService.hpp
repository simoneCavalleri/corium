#pragma once

#include "corium/IEventSink.hpp"
#include "corium/ServiceContext.hpp"

#include <stop_token>
#include <utility>

namespace corium {

/// @brief Interface for asynchronous background services.
class IBackgroundService {
public:
    virtual ~IBackgroundService() = default;

    /// @brief Execution loop run on a background jthread.
    /// @param stopToken Cancellation token to check for graceful shutdown.
    virtual void run(std::stop_token stopToken) = 0;
};

/// @brief Base class for background services storing ServiceContext.
class BackgroundService : public IBackgroundService {
public:
    explicit BackgroundService(const ServiceContext& context)
        : _context(context)
    {}

    ~BackgroundService() override = default;

protected:
    /// @brief Access service context.
    [[nodiscard]] ServiceContext& context() noexcept { return _context; }
    [[nodiscard]] const ServiceContext& context() const noexcept { return _context; }

    /// @brief Access event sink for posting events.
    [[nodiscard]] IEventSink& events() noexcept { return _context.eventSink; }
    [[nodiscard]] IEventSink& eventSink() noexcept { return _context.eventSink; }

    /// @brief Helper method to post an event to the main event queue.
    /// @tparam EventType Type of the event.
    /// @param event Event instance to post.
    template <typename EventType>
    void postEvent(EventType&& event)
    {
        _context.eventSink.post(std::forward<EventType>(event));
    }

private:
    ServiceContext _context;
};

} // namespace corium
