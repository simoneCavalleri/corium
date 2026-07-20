#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <new>
#include <utility>

namespace corium {

/// @brief Lock-free Multiple-Producer, Single-Consumer (MPSC) RingBuffer.
/// Implements Dmitry Vyukov's algorithm with zero heap allocations (uses std::array).
/// Cache-line aligned (alignas(64)) to eliminate false sharing.
/// @tparam T Event element type stored in ring cells.
/// @tparam Capacity Buffer capacity (must be a power of 2).
template <typename T, size_t Capacity>
class MpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2.");

    struct alignas(64) Cell {
        std::atomic<size_t> sequence;
        alignas(alignof(T)) char storage[sizeof(T)];

        template <typename... Args>
        void construct(Args&&... args) {
            new (storage) T(std::forward<Args>(args)...);
        }

        T& value() noexcept {
            return *reinterpret_cast<T*>(storage);
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
        for (size_t i = 0; i < Capacity; ++i) {
            _buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
        _enqueuePos.store(0, std::memory_order_relaxed);
        _dequeuePos.store(0, std::memory_order_relaxed);
    }

    ~MpscRingBuffer() {
        size_t pos = _dequeuePos.load(std::memory_order_relaxed);
        size_t end = _enqueuePos.load(std::memory_order_relaxed);
        while (pos < end) {
            _buffer[pos & Mask].destroy();
            pos++;
        }
    }

    MpscRingBuffer(const MpscRingBuffer&) = delete;
    MpscRingBuffer& operator=(const MpscRingBuffer&) = delete;

    /// @brief Try to push an item into the ring buffer (thread-safe for multiple producers).
    PushResult tryPush(T&& value) {
        size_t pos = _enqueuePos.load(std::memory_order_relaxed);
        Cell* cell;
        for (;;) {
            cell = &_buffer[pos & Mask];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (diff == 0) {
                if (_enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return {false, false};
            } else {
                pos = _enqueuePos.load(std::memory_order_relaxed);
            }
        }

        cell->construct(std::move(value));
        cell->sequence.store(pos + 1, std::memory_order_release);
        
        size_t readPos = _dequeuePos.load(std::memory_order_acquire);
        bool wasEmpty = (pos == readPos);

        return {true, wasEmpty};
    }

    /// @brief Try to pop an item from the ring buffer (single consumer thread).
    bool tryPop(T& value) {
        size_t pos = _dequeuePos.load(std::memory_order_relaxed);
        Cell* cell = &_buffer[pos & Mask];
        size_t seq = cell->sequence.load(std::memory_order_acquire);
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

        if (diff == 0) {
            _dequeuePos.store(pos + 1, std::memory_order_relaxed);
            value = std::move(cell->value());
            cell->destroy();
            cell->sequence.store(pos + Mask + 1, std::memory_order_release);
            return true;
        }

        return false;
    }

    /// @brief Check if ring buffer is empty.
    [[nodiscard]] bool empty() const noexcept {
        size_t writePos = _enqueuePos.load(std::memory_order_acquire);
        size_t readPos = _dequeuePos.load(std::memory_order_relaxed);
        return writePos == readPos;
    }

private:
    static constexpr size_t Mask = Capacity - 1;

    alignas(64) std::array<Cell, Capacity> _buffer;
    alignas(64) std::atomic<size_t> _enqueuePos;
    alignas(64) std::atomic<size_t> _dequeuePos;
};

} // namespace corium
