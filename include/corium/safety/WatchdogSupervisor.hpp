/**
 * @file WatchdogSupervisor.hpp
 * @ingroup safety
 * @brief Multi-task SLA deadline monitor controlling hardware watchdog refresh.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "corium/safety/HeartbeatMonitor.hpp"
#include "corium/safety/SafetyEvents.hpp"
#include "corium/timers/ClockPolicies.hpp"

namespace corium::safety {

/// @ingroup safety
/// @brief Dedicated safety supervisor monitoring subsystem heartbeats and controlling watchdog refresh.
/// Compatible with hardware IWDG (STM32, ESP32, nRF), RTOS Task Watchdogs, and POSIX watchdogs.
/// Zero dynamic heap allocations, zero threading overhead.
/// @tparam MaxServices Maximum number of monitored services.
/// @tparam ClockPolicy Time source policy (defaults to ChronoClockPolicy).
template <
    std::size_t MaxServices = 16,
    typename ClockPolicy = ChronoClockPolicy
>
class WatchdogSupervisor {
public:
    using KickCallbackFn = void (*)(void* userData);

    WatchdogSupervisor() = default;

    /// @brief Configure the physical hardware/software watchdog kick callback.
    /// @param kickFn Function called to refresh the watchdog timer.
    /// @param userData Pointer passed to the kick function.
    void setWatchdogKickCallback(KickCallbackFn kickFn, void* userData = nullptr) noexcept
    {
        _kickFn = kickFn;
        _userData = userData;
    }

    /// @brief Access reference to the internal HeartbeatMonitor.
    [[nodiscard]] HeartbeatMonitor<MaxServices>& heartbeatMonitor() noexcept
    {
        return _monitor;
    }

    /// @brief Access const reference to the internal HeartbeatMonitor.
    [[nodiscard]] const HeartbeatMonitor<MaxServices>& heartbeatMonitor() const noexcept
    {
        return _monitor;
    }

    /// @brief Register a service for supervision.
    bool registerService(uint32_t serviceId, uint64_t timeoutNs) noexcept
    {
        const uint64_t now = nowNs();
        return _monitor.registerService(serviceId, timeoutNs, now);
    }

    /// @brief Submit a heartbeat for a monitored service.
    void beat(uint32_t serviceId) noexcept
    {
        _monitor.beat(serviceId, nowNs());
    }

    /// @brief Perform a supervisor check iteration.
    /// Kicks the hardware watchdog if healthy; suppresses the kick and dispatches WatchdogTimeoutEvent if failed.
    /// @tparam EventSinkType Event sink type to post failure events.
    /// @param sink Event sink handle to dispatch emergency events.
    /// @return true if all services were healthy and watchdog was kicked; false if watchdog was suppressed.
    template <typename EventSinkType>
    bool supervise(const EventSinkType& sink) noexcept
    {
        const uint64_t now = nowNs();
        uint32_t timedOutId = 0;
        uint64_t lastBeat = 0;
        uint64_t budget = 0;

        if (_monitor.checkHealth(now, timedOutId, lastBeat, budget)) {
            // System is healthy: feed the watchdog
            if (_kickFn) {
                _kickFn(_userData);
            }
            _kicksCount.fetch_add(1, std::memory_order_relaxed);
            _lastHealthyTimeNs.store(now, std::memory_order_release);
            return true;
        }

        // Failure detected: suppress watchdog kick and post emergency event
        _suppressionsCount.fetch_add(1, std::memory_order_relaxed);
        sink.postHighPriority(WatchdogTimeoutEvent{.serviceId = timedOutId, .lastHeartbeatNs = lastBeat, .timeoutBudgetNs = budget});
        return false;
    }

    /// @brief Get total number of successful watchdog kicks.
    [[nodiscard]] uint64_t totalKicks() const noexcept
    {
        return _kicksCount.load(std::memory_order_relaxed);
    }

    /// @brief Get total number of watchdog kick suppressions due to health violations.
    [[nodiscard]] uint64_t totalSuppressions() const noexcept
    {
        return _suppressionsCount.load(std::memory_order_relaxed);
    }

private:
    [[nodiscard]] static uint64_t nowNs() noexcept
    {
        if constexpr (std::is_integral_v<typename ClockPolicy::time_point>) {
            return static_cast<uint64_t>(ClockPolicy::now());
        } else {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    ClockPolicy::now().time_since_epoch()
                ).count()
            );
        }
    }

    HeartbeatMonitor<MaxServices> _monitor{};
    KickCallbackFn _kickFn{nullptr};
    void* _userData{nullptr};
    std::atomic<uint64_t> _kicksCount{0};
    std::atomic<uint64_t> _suppressionsCount{0};
    std::atomic<uint64_t> _lastHealthyTimeNs{0};
};

} // namespace corium::safety
