/**
 * @file DmaUartAdapter.hpp
 * @ingroup embedded
 * @brief Zero-copy circular DMA UART receiver adapter for STM32, ESP32, and bare-metal UART ISRs.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "corium/EventSink.hpp"
#include "corium/policies/QueuePolicies.hpp"

namespace corium::embedded {

/// @brief Statically-allocated circular DMA UART RX buffer.
/// @tparam BufferSize Circular buffer capacity in bytes (must be power of two).
template <std::size_t BufferSize = 512>
class DmaUartRxBuffer {
    static_assert((BufferSize & (BufferSize - 1)) == 0, "BufferSize must be a power of two.");

public:
    constexpr DmaUartRxBuffer() = default;

    /// @brief Direct pointer to raw memory buffer for DMA peripheral configuration (e.g. HAL_UART_Receive_DMA).
    [[nodiscard]] uint8_t* dmaBuffer() noexcept {
        return _buffer.data();
    }

    /// @brief Const pointer to raw memory buffer.
    [[nodiscard]] const uint8_t* dmaBuffer() const noexcept {
        return _buffer.data();
    }

    /// @brief Capacity of circular DMA buffer in bytes.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return BufferSize;
    }

    /// @brief Update tail position based on hardware DMA counter and extract available bytes.
    /// Typically invoked from UART Idle Line ISR or DMA Half/Full Transfer ISR.
    /// @param currentDmaWriteIndex Current hardware DMA write index (e.g. BufferSize - __HAL_DMA_GET_COUNTER(hdma)).
    /// @param onPacket Extracted byte span callback: void(std::span<const uint8_t> data).
    template <typename Callback>
    void processAvailableBytes(std::size_t currentDmaWriteIndex, Callback&& onPacket) {
        currentDmaWriteIndex = currentDmaWriteIndex & (BufferSize - 1);

        if (currentDmaWriteIndex == _tail) {
            return; // No new bytes received
        }

        if (currentDmaWriteIndex > _tail) {
            // Contiguous slice
            std::size_t len = currentDmaWriteIndex - _tail;
            onPacket(std::span<const uint8_t>(_buffer.data() + _tail, len));
            _tail = currentDmaWriteIndex;
        } else {
            // Wrapped around circular buffer
            std::size_t firstPart = BufferSize - _tail;
            if (firstPart > 0) {
                onPacket(std::span<const uint8_t>(_buffer.data() + _tail, firstPart));
            }
            if (currentDmaWriteIndex > 0) {
                onPacket(std::span<const uint8_t>(_buffer.data(), currentDmaWriteIndex));
            }
            _tail = currentDmaWriteIndex;
        }
    }

    /// @brief Reset circular tail index.
    void reset() noexcept {
        _tail = 0;
    }

    /// @brief Current tail index in the circular buffer.
    [[nodiscard]] std::size_t tail() const noexcept {
        return _tail;
    }

private:
    alignas(4) std::array<uint8_t, BufferSize> _buffer{};
    std::size_t _tail{0};
};

/// @ingroup embedded
/// @brief Non-blocking hardware ISR adapter for DMA UART controllers.
template <typename EventVariant, std::size_t BufferSize = 512>
class DmaUartAdapter {
public:
    explicit DmaUartAdapter(EventSinkT<EventVariant> sink) noexcept
        : _sink(sink)
    {}

    /// @brief Access underlying circular DMA RX buffer.
    [[nodiscard]] DmaUartRxBuffer<BufferSize>& rxBuffer() noexcept {
        return _rxBuffer;
    }

    /// @brief Access const circular DMA RX buffer.
    [[nodiscard]] const DmaUartRxBuffer<BufferSize>& rxBuffer() const noexcept {
        return _rxBuffer;
    }

    /// @brief Process newly received DMA bytes and post constructed event into sink.
    /// @tparam TargetEvent User event constructible from std::span<const uint8_t>.
    /// @param currentDmaWriteIndex Hardware DMA write pointer position.
    /// @param priority Priority to assign to generated events.
    template <typename TargetEvent>
    void processFromIsr(std::size_t currentDmaWriteIndex, EventPriority priority = EventPriority::Normal) {
        _rxBuffer.processAvailableBytes(currentDmaWriteIndex, [this, priority](std::span<const uint8_t> data) {
            _sink.post(EventVariant{TargetEvent{data}}, priority);
        });
    }

private:
    EventSinkT<EventVariant> _sink;
    DmaUartRxBuffer<BufferSize> _rxBuffer{};
};

} // namespace corium::embedded
