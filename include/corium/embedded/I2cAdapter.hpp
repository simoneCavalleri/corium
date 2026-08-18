/**
 * @file I2cAdapter.hpp
 * @ingroup embedded
 * @brief Zero-heap I2C bus hardware ISR adapter for sensors and peripherals.
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

/// @brief I2C transaction direction/operation type.
enum class I2cOpType : uint8_t {
    Read = 0,
    Write = 1,
    WriteRead = 2
};

/// @brief Zero-heap I2C transaction result frame.
/// @tparam MaxPayload Maximum payload capacity in bytes (default: 32).
template <size_t MaxPayload = 32>
struct I2cFrame {
    uint16_t deviceAddress{0};     ///< 7-bit or 10-bit I2C target slave address
    uint8_t registerAddress{0};    ///< Internal register address (if applicable)
    I2cOpType opType{I2cOpType::Read}; ///< Read, Write, or WriteRead
    uint8_t status{0};             ///< Hardware status (0: ACK/Success, 1: NACK, 2: Bus Error)
    uint8_t length{0};             ///< Number of valid data bytes
    std::array<uint8_t, MaxPayload> data{}; ///< Payload buffer

    [[nodiscard]] std::span<const uint8_t> payload() const noexcept {
        return {data.data(), static_cast<size_t>(length > MaxPayload ? MaxPayload : length)};
    }
};

/// @ingroup embedded
/// @brief Non-blocking hardware ISR adapter for I2C master/slave controllers.
/// Dispatches completed I2C read/write transactions directly into the Corium lock-free ring buffer.
template <typename EventVariant>
class I2cIsrAdapter {
public:
    explicit I2cIsrAdapter(EventSinkT<EventVariant> sink) noexcept
        : m_sink(sink)
    {}

    /// @brief Post an I2C frame from ISR context into the event sink.
    /// @tparam TargetEvent User event type constructible from I2cFrame or I2cFrame itself.
    /// @tparam MaxPayload Payload capacity of the frame.
    /// @param frame The completed I2C transaction frame.
    /// @param priority Priority to assign.
    template <typename TargetEvent = I2cFrame<32>, size_t MaxPayload = 32>
    void postFromIsr(const I2cFrame<MaxPayload>& frame, EventPriority priority = EventPriority::Normal) noexcept {
        if constexpr (std::is_same_v<TargetEvent, I2cFrame<MaxPayload>>) {
            m_sink.post(EventVariant{frame}, priority);
        } else {
            m_sink.post(EventVariant{TargetEvent{frame}}, priority);
        }
    }

    /// @brief Construct and post an I2C read result frame from ISR context.
    /// @tparam TargetEvent User event type.
    /// @param devAddr Slave device address.
    /// @param regAddr Register address read from.
    /// @param readBytes Span of received bytes.
    /// @param status Hardware status code.
    /// @param priority Event priority.
    template <typename TargetEvent = I2cFrame<32>>
    void postReadFromIsr(
        uint16_t devAddr,
        uint8_t regAddr,
        std::span<const uint8_t> readBytes,
        uint8_t status = 0,
        EventPriority priority = EventPriority::Normal
    ) noexcept {
        I2cFrame<32> frame{};
        frame.deviceAddress = devAddr;
        frame.registerAddress = regAddr;
        frame.opType = I2cOpType::Read;
        frame.status = status;
        frame.length = static_cast<uint8_t>(readBytes.size() > 32 ? 32 : readBytes.size());
        std::memcpy(frame.data.data(), readBytes.data(), frame.length);

        if constexpr (std::is_same_v<TargetEvent, I2cFrame<32>>) {
            m_sink.post(EventVariant{frame}, priority);
        } else {
            m_sink.post(EventVariant{TargetEvent{frame}}, priority);
        }
    }

private:
    EventSinkT<EventVariant> m_sink;
};

} // namespace corium::embedded
