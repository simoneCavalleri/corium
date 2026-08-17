#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace corium::safety {

/// @brief Zero-heap multi-service heartbeat tracker for safety-critical execution.
/// Thread-safe for concurrent heartbeat signals and supervisory health checks.
/// @tparam MaxServices Maximum number of monitored services (default: 16).
template <std::size_t MaxServices = 16>
class HeartbeatMonitor {
public:
    struct alignas(32) ServiceEntry {
        uint32_t serviceId{0};
        uint64_t timeoutNs{0};
        std::atomic<uint64_t> lastHeartbeatNs{0};
        std::atomic<bool> active{false};
    };

    HeartbeatMonitor() = default;

    /// @brief Register a service for heartbeat monitoring with a specified timeout.
    /// @param serviceId Unique numeric identifier for the service.
    /// @param timeoutNs Maximum allowed duration between heartbeats in nanoseconds.
    /// @param initialTimestampNs Initial timestamp to baseline the service.
    /// @return true if successfully registered, false if capacity exceeded.
    bool registerService(uint32_t serviceId, uint64_t timeoutNs, uint64_t initialTimestampNs = 0) noexcept
    {
        for (std::size_t i = 0; i < MaxServices; ++i) {
            bool expected = false;
            if (_services[i].active.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
                _services[i].serviceId = serviceId;
                _services[i].timeoutNs = timeoutNs;
                _services[i].lastHeartbeatNs.store(initialTimestampNs, std::memory_order_release);
                return true;
            }
            if (_services[i].serviceId == serviceId && expected) {
                // Already registered; update timeout and baseline
                _services[i].timeoutNs = timeoutNs;
                _services[i].lastHeartbeatNs.store(initialTimestampNs, std::memory_order_release);
                return true;
            }
        }
        return false;
    }

    /// @brief Record a heartbeat for a given service.
    /// @param serviceId Unique service identifier.
    /// @param nowNs Current timestamp in nanoseconds.
    void beat(uint32_t serviceId, uint64_t nowNs) noexcept
    {
        for (std::size_t i = 0; i < MaxServices; ++i) {
            if (_services[i].active.load(std::memory_order_relaxed) && _services[i].serviceId == serviceId) {
                _services[i].lastHeartbeatNs.store(nowNs, std::memory_order_release);
                return;
            }
        }
    }

    /// @brief Unregister a service from active monitoring.
    void unregisterService(uint32_t serviceId) noexcept
    {
        for (std::size_t i = 0; i < MaxServices; ++i) {
            if (_services[i].serviceId == serviceId) {
                _services[i].active.store(false, std::memory_order_release);
                return;
            }
        }
    }

    /// @brief Check health status of all registered active services.
    /// @param nowNs Current timestamp in nanoseconds.
    /// @param outTimedOutServiceId Populated with the ID of the first failing service if unhealthy.
    /// @param outLastHeartbeatNs Populated with the timestamp of the last received heartbeat if unhealthy.
    /// @param outTimeoutBudgetNs Populated with the allowed timeout budget if unhealthy.
    /// @return true if all services are within their heartbeat SLA; false otherwise.
    [[nodiscard]] bool checkHealth(
        uint64_t nowNs,
        uint32_t& outTimedOutServiceId,
        uint64_t& outLastHeartbeatNs,
        uint64_t& outTimeoutBudgetNs
    ) const noexcept
    {
        for (std::size_t i = 0; i < MaxServices; ++i) {
            if (_services[i].active.load(std::memory_order_acquire)) {
                const uint64_t last = _services[i].lastHeartbeatNs.load(std::memory_order_acquire);
                const uint64_t limit = _services[i].timeoutNs;
                if (nowNs > last && (nowNs - last) > limit) {
                    outTimedOutServiceId = _services[i].serviceId;
                    outLastHeartbeatNs = last;
                    outTimeoutBudgetNs = limit;
                    return false;
                }
            }
        }
        return true;
    }

    /// @brief Get maximum capacity of monitored services.
    [[nodiscard]] constexpr std::size_t maxServices() const noexcept
    {
        return MaxServices;
    }

private:
    std::array<ServiceEntry, MaxServices> _services{};
};

} // namespace corium::safety
