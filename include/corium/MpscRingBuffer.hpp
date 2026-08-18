/**
 * @file MpscRingBuffer.hpp
 * @ingroup core
 * @brief Lock-free Multi-Producer Single-Consumer (MPSC) bounded ring buffer based on Dmitry Vyukov's algorithm.
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace corium {

/// @ingroup core
/// @brief Lock-free Multiple-Producer, Single-Consumer (MPSC) RingBuffer.
/// Implements Dmitry Vyukov's algorithm with zero heap allocations (uses std::array).
/// Cache-line aligned (alignas(64)) to eliminate false sharing.
/// @tparam T Event element type stored in ring cells.
/// @tparam Capacity Buffer capacity (must be a power of 2).
template <typename T, std::size_t Capacity>
class MpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2.");

    struct alignas(64) Cell {
        std::atomic<std::size_t> sequence;
        alignas(alignof(T)) std::byte storage[sizeof(T)];

        template <typename... Args>
        void construct(Args&&... args) {
            new (static_cast<void*>(storage)) T(std::forward<Args>(args)...);
        }

        [[nodiscard]] T& value() noexcept {
            return *std::launder(reinterpret_cast<T*>(storage));
        }

        [[nodiscard]] const T& value() const noexcept {
            return *std::launder(reinterpret_cast<const T*>(storage));
        }

        void destroy() noexcept {
            value().~T();
        }
    };

public:
    struct PushResult {
        bool pushed;
        bool wasEmpty;
    };

    MpscRingBuffer() {
        for (std::size_t i = 0; i < Capacity; ++i) {
            _buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
        _enqueuePos.store(0, std::memory_order_relaxed);
        _dequeuePos.store(0, std::memory_order_relaxed);
    }

    ~MpscRingBuffer() {
        T dummy;
        while (tryPop(dummy)) {}
    }

    MpscRingBuffer(const MpscRingBuffer&) = delete;
    MpscRingBuffer& operator=(const MpscRingBuffer&) = delete;

    MpscRingBuffer(MpscRingBuffer&&) = delete;
    MpscRingBuffer& operator=(MpscRingBuffer&&) = delete;

    /// @brief Push an item into the queue (Multi-Producer thread safe).
    /// @return PushResult indicating success and whether queue was empty before push.
    template <typename... Args>
    PushResult tryPush(Args&&... args) {
        Cell* cell = nullptr;
        std::size_t pos = _enqueuePos.load(std::memory_order_relaxed);

        for (;;) {
            cell = &_buffer[pos & Mask];
            std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                if (_enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break; // Reserved cell
                }
            } else if (diff < 0) {
                return {false, false}; // Full
            } else {
                pos = _enqueuePos.load(std::memory_order_relaxed);
            }
        }

        cell->construct(std::forward<Args>(args)...);
        cell->sequence.store(pos + 1, std::memory_order_release);

        const bool wasEmpty = (pos == _dequeuePos.load(std::memory_order_relaxed));
        return {true, wasEmpty};
    }

    /// @brief Pop an item from the queue (Single-Consumer only).
    /// @return true if an item was successfully popped; false if empty.
    bool tryPop(T& result) {
        Cell* cell = nullptr;
        std::size_t pos = _dequeuePos.load(std::memory_order_relaxed);

        cell = &_buffer[pos & Mask];
        std::size_t seq = cell->sequence.load(std::memory_order_acquire);
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

        if (diff == 0) {
            _dequeuePos.store(pos + 1, std::memory_order_relaxed);
            result = std::move(cell->value());
            cell->destroy();
            cell->sequence.store(pos + Mask + 1, std::memory_order_release);
            return true;
        }

        return false;
    }

    /// @brief Check if the queue is empty (approximate if concurrent producers are active).
    [[nodiscard]] bool empty() const noexcept {
        const std::size_t pos = _dequeuePos.load(std::memory_order_relaxed);
        const Cell* cell = &_buffer[pos & Mask];
        const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
        const intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
        return diff != 0;
    }

    /// @brief Return fixed capacity of this ring buffer.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

private:
    static constexpr std::size_t Mask = Capacity - 1;

    alignas(64) std::array<Cell, Capacity> _buffer;
    alignas(64) std::atomic<std::size_t> _enqueuePos;
    alignas(64) std::atomic<std::size_t> _dequeuePos;
};

namespace internal {
    template <typename T, std::size_t Capacity>
    using MpscRingBuffer = ::corium::MpscRingBuffer<T, Capacity>;
} // namespace internal

} // namespace corium
