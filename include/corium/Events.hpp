#pragma once

#include <cstdint>
#include <variant>

namespace corium {

/// @brief Application shutdown event.
struct QuitEvent {};

/// @brief Periodic heartbeat or hardware timer tick event.
struct TickEvent {
    uint64_t tick = 0;
    double deltaTime = 0.0;

    constexpr TickEvent() = default;
    constexpr explicit TickEvent(double dt, uint64_t t = 0)
        : tick(t), deltaTime(dt) {}
};

/// @brief Logical update or execution step event.
struct UpdateEvent {
    double deltaTime = 0.0;

    constexpr UpdateEvent() = default;
    constexpr explicit UpdateEvent(double dt)
        : deltaTime(dt) {}
};

/// @brief System, hardware, or framework error event.
struct ErrorEvent {
    uint32_t code = 0;
    uintptr_t payload = 0;

    constexpr ErrorEvent() = default;
    constexpr ErrorEvent(uint32_t errCode, uintptr_t data = 0)
        : code(errCode), payload(data) {}
};

/// @brief Generic signal or notification trigger event.
struct SignalEvent {
    uint32_t id = 0;

    constexpr SignalEvent() = default;
    constexpr explicit SignalEvent(uint32_t signalId)
        : id(signalId) {}
};

/// @brief Default variant list of core Corium events.
using DefaultEvents = std::variant<
    QuitEvent,
    TickEvent,
    UpdateEvent,
    ErrorEvent,
    SignalEvent
>;

/// @brief Alias for DefaultEvents.
using Event = DefaultEvents;

} // namespace corium
