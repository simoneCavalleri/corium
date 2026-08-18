/**
 * @file SpiAdapter.hpp
 * @ingroup embedded
 * @brief Zero-heap SPI hardware ISR and DMA completion adapter for embedded sensors (IMU, ADC, Flash).
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

#include "corium/EventSink.hpp"
#include "corium/policies/QueuePolicies.hpp"

namespace corium::embedded {

/// @brief Fixed-capacity SPI transfer frame structure for zero-heap ISR and DMA ingestion.
/// @tparam MaxRxSize Maximum receive buffer size in bytes (default: 32).
template <size_t MaxRxSize = 32>
struct SpiFrame {
    uint8_t chipSelectId{0};       ///< Chip-select / device channel identifier
    uint16_t length{0};            ///< Number of valid received bytes
    uint8_t status{0};             ///< Hardware status flags (0: Success, 1: Error/Timeout)
    std::array<uint8_t, MaxRxSize> rxData{}; ///< Static receive buffer

    [[nodiscard]] std::span<const uint8_t> payload() const noexcept {
        return {rxData.data(), static_cast<size_t>(length > MaxRxSize ? MaxRxSize : length)};
    }
};

/// @ingroup embedded
/// @brief Non-blocking hardware ISR adapter for SPI controllers and DMA completion interrupts.
/// Ingests SPI frames from interrupt context and posts typed events into Corium lock-free ring buffer.
template <typename EventVariant>
class SpiIsrAdapter {
public:
    explicit SpiIsrAdapter(EventSinkT<EventVariant> sink) noexcept
        : m_sink(sink)
    {}

    /// @brief Post an SPI frame directly from ISR context into the event sink.
    /// @tparam TargetEvent User event type constructible from SpiFrame or SpiFrame itself.
    /// @tparam MaxRxSize Buffer capacity of the SPI frame.
    /// @param frame The completed SPI hardware frame.
    /// @param priority Priority to assign (default: Normal).
    template <typename TargetEvent = SpiFrame<32>, size_t MaxRxSize = 32>
    void postFromIsr(const SpiFrame<MaxRxSize>& frame, EventPriority priority = EventPriority::Normal) noexcept {
        if constexpr (std::is_same_v<TargetEvent, SpiFrame<MaxRxSize>>) {
            m_sink.post(EventVariant{frame}, priority);
        } else {
            m_sink.post(EventVariant{TargetEvent{frame}}, priority);
        }
    }

    /// @brief Ingest raw SPI RX bytes from a DMA buffer into a typed event and post into sink.
    /// @tparam TargetEvent User event type constructible from SpiFrame.
    /// @param csId Chip-select ID of the SPI slave.
    /// @param rxBytes Span of received bytes.
    /// @param status Hardware status code.
    /// @param priority Event priority.
    template <typename TargetEvent = SpiFrame<32>>
    void postBufferFromIsr(
        uint8_t csId,
        std::span<const uint8_t> rxBytes,
        uint8_t status = 0,
        EventPriority priority = EventPriority::Normal
    ) noexcept {
        SpiFrame<32> frame{};
        frame.chipSelectId = csId;
        frame.status = status;
        frame.length = static_cast<uint16_t>(rxBytes.size() > 32 ? 32 : rxBytes.size());
        std::memcpy(frame.rxData.data(), rxBytes.data(), frame.length);

        if constexpr (std::is_same_v<TargetEvent, SpiFrame<32>>) {
            m_sink.post(EventVariant{frame}, priority);
        } else {
            m_sink.post(EventVariant{TargetEvent{frame}}, priority);
        }
    }

private:
    EventSinkT<EventVariant> m_sink;
};

} // namespace corium::embedded
