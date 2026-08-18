/**
 * @file CanAdapter.hpp
 * @ingroup embedded
 * @brief Zero-heap CAN 2.0B and CAN-FD hardware ISR adapter for STM32 FDCAN, ESP32 TWAI, and CMSIS.
 */

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "corium/EventSink.hpp"
#include "corium/policies/QueuePolicies.hpp"

namespace corium::embedded {

/// @brief Standard CAN 2.0B classic frame structure (up to 8 bytes payload).
struct CanFrame {
    uint32_t id{0};               ///< 11-bit standard or 29-bit extended identifier
    uint8_t dlc{0};               ///< Data Length Code (0..8)
    bool isExtended{false};       ///< True if 29-bit extended ID
    bool isRtr{false};            ///< Remote Transmission Request flag
    std::array<uint8_t, 8> data{}; ///< Payload bytes

    [[nodiscard]] std::span<const uint8_t> payload() const noexcept {
        return {data.data(), static_cast<std::size_t>(dlc > 8 ? 8 : dlc)};
    }
};

/// @brief ISO 11898-1:2015 CAN-FD flexible data-rate frame structure (up to 64 bytes payload).
struct CanFdFrame {
    uint32_t id{0};                ///< 11-bit standard or 29-bit extended identifier
    uint8_t len{0};                ///< Data length in bytes (0..64)
    bool isExtended{false};        ///< True if 29-bit extended ID
    bool bitRateSwitch{false};     ///< BRS: Bit Rate Switch flag
    bool errorStateIndicator{false}; ///< ESI: Error State Indicator flag
    std::array<uint8_t, 64> data{}; ///< Payload bytes (up to 64B)

    [[nodiscard]] std::span<const uint8_t> payload() const noexcept {
        return {data.data(), static_cast<std::size_t>(len > 64 ? 64 : len)};
    }
};

/// @ingroup embedded
/// @brief Non-blocking hardware ISR adapter for CAN/CAN-FD controller interrupts.
/// Ingests hardware mailbox/FIFO frames inside ISR and posts into Corium lock-free ring buffer.
template <typename EventVariant>
class CanIsrAdapter {
public:
    explicit CanIsrAdapter(EventSinkT<EventVariant> sink) noexcept
        : _sink(sink)
    {}

    /// @brief Ingest standard CAN frame from hardware interrupt (e.g. STM32 CAN_RX_IRQHandler).
    /// @tparam TargetEvent User-defined event type constructible from CanFrame.
    /// @param frame Raw hardware CAN frame.
    /// @param priority Priority to assign (default Normal, High for emergency/E-Stop).
    template <typename TargetEvent = CanFrame>
    void postFromIsr(const CanFrame& frame, EventPriority priority = EventPriority::Normal) noexcept {
        if constexpr (std::is_same_v<TargetEvent, CanFrame>) {
            _sink.post(EventVariant{frame}, priority);
        } else {
            _sink.post(EventVariant{TargetEvent{frame}}, priority);
        }
    }

    /// @brief Ingest high-speed CAN-FD frame from hardware interrupt (e.g. STM32 FDCAN1_IT0_IRQHandler).
    /// @tparam TargetEvent User-defined event type constructible from CanFdFrame.
    /// @param frame Raw hardware CAN-FD frame.
    /// @param priority Priority to assign.
    template <typename TargetEvent = CanFdFrame>
    void postFromIsr(const CanFdFrame& frame, EventPriority priority = EventPriority::Normal) noexcept {
        if constexpr (std::is_same_v<TargetEvent, CanFdFrame>) {
            _sink.post(EventVariant{frame}, priority);
        } else {
            _sink.post(EventVariant{TargetEvent{frame}}, priority);
        }
    }

private:
    EventSinkT<EventVariant> _sink;
};

} // namespace corium::embedded
