#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <thread>

#include "corium/BackgroundService.hpp"
#include "corium/safety/WatchdogSupervisor.hpp"
#include "corium/timers/ClockPolicies.hpp"

namespace corium::safety {

/// @ingroup safety
/// @brief Multi-threaded background safety service executing periodic heartbeat supervision.
/// Automatically verifies monitored task deadlines and dispatches WatchdogTimeoutEvent to the main event bus on violations.
/// @tparam EventVariant Supported event variant list type.
/// @tparam MaxServices Maximum number of monitored services (default 16).
/// @tparam ClockPolicy Clock source strategy (defaults to ChronoClockPolicy).
template <
    typename EventVariant = DefaultEvents,
    std::size_t MaxServices = 16,
    typename ClockPolicy = ChronoClockPolicy
>
class WatchdogService : public BackgroundService<EventVariant> {
public:
    using SupervisorType = WatchdogSupervisor<MaxServices, ClockPolicy>;
    using KickCallbackFn = typename SupervisorType::KickCallbackFn;

    explicit WatchdogService(std::chrono::milliseconds checkInterval = std::chrono::milliseconds(20))
        : _checkInterval(checkInterval)
    {}

    /// @brief Set callback to refresh the physical hardware watchdog (e.g. STM32 IWDG).
    void setWatchdogKickCallback(KickCallbackFn kickFn, void* userData = nullptr) noexcept
    {
        _supervisor.setWatchdogKickCallback(kickFn, userData);
    }

    /// @brief Register a service for supervision with maximum timeout in nanoseconds.
    bool registerService(uint32_t serviceId, uint64_t timeoutNs) noexcept
    {
        return _supervisor.registerService(serviceId, timeoutNs);
    }

    /// @brief Submit a heartbeat for a monitored service.
    void beat(uint32_t serviceId) noexcept
    {
        _supervisor.beat(serviceId);
    }

    /// @brief Access underlying WatchdogSupervisor.
    [[nodiscard]] SupervisorType& supervisor() noexcept
    {
        return _supervisor;
    }

    [[nodiscard]] const SupervisorType& supervisor() const noexcept
    {
        return _supervisor;
    }

    /// @brief Background worker loop executing periodic health supervision.
    void run(const std::stop_token& stopToken)
    {
        while (!stopToken.stop_requested()) {
            std::this_thread::sleep_for(_checkInterval);
            _supervisor.supervise(this->context().mainSink());
        }
    }

private:
    std::chrono::milliseconds _checkInterval;
    SupervisorType _supervisor;
};

} // namespace corium::safety
