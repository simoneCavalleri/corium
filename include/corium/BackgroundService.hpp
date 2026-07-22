#pragma once

#include "corium/IEventSink.hpp"
#include "corium/ServiceContext.hpp"

#include <stop_token>
#include <thread>
#include <utility>

namespace corium {

/// @brief Non-allocating base class for background services owning a dedicated C++20 std::jthread.
/// Zero heap allocations, zero vtables/RTTI.
/// @tparam EventVariant Supported event variant type list.
template <typename EventVariant = DefaultEvents>
class BackgroundService {
public:
    BackgroundService() = default;

    explicit BackgroundService(ServiceContextT<EventVariant> context)
        : _context(context)
    {}

    ~BackgroundService()
    {
        stop();
        join();
    }

    BackgroundService(const BackgroundService&) = delete;
    BackgroundService& operator=(const BackgroundService&) = delete;

    void setContext(ServiceContextT<EventVariant> context) noexcept
    {
        _context = context;
    }

    /// @brief Start execution loop on dedicated std::jthread.
    template <typename Derived>
    void startThread(Derived* derived)
    {
        _thread = std::jthread([this, derived](std::stop_token stopToken) {
#if __cpp_exceptions
            try {
                derived->run(stopToken);
            } catch (...) {
            }
#else
            derived->run(stopToken);
#endif
        });
    }

    /// @brief Request graceful stop of the background thread via std::stop_token.
    void stop() noexcept
    {
        _thread.request_stop();
    }

    /// @brief Join background std::jthread cleanly.
    void join() noexcept
    {
        if (_thread.joinable()) {
            _thread.join();
        }
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
    std::jthread _thread;
};

} // namespace corium
