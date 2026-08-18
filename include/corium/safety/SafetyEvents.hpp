/**
 * @file SafetyEvents.hpp
 * @ingroup safety
 * @brief Heartbeat and fault notification event structures.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace corium::safety {

/// @brief Dispatched when a monitored service fails to deliver its heartbeat within its timeout window.
struct WatchdogTimeoutEvent {
    uint32_t serviceId{0};
    uint64_t lastHeartbeatNs{0};
    uint64_t timeoutBudgetNs{0};
};

/// @brief Dispatched when an event handler or task misses its real-time deadline budget.
struct DeadlineMissedEvent {
    std::size_t eventTypeId{0};
    const char* eventName{nullptr};
    double actualDurationUs{0.0};
    double maxAllowedDurationUs{0.0};
};

/// @brief Dispatched when a CircuitBreaker detects repeated failures and trips open.
struct CircuitBreakerTrippedEvent {
    uint32_t componentId{0};
    uint32_t failureCount{0};
    uint64_t recoveryCooldownMs{0};
};

} // namespace corium::safety
