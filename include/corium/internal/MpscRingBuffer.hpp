#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace corium {

template <typename T, size_t Capacity = 1024>
class MpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

    static constexpr size_t CacheLineSize = 64;

    struct alignas(CacheLineSize) Cell {
        std::atomic<size_t> sequence;
        T data;
    };

public:
    MpscRingBuffer()
        : _mask(Capacity - 1)
    {
        for (size_t i = 0; i < Capacity; ++i) {
            _buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
        _enqueuePos.store(0, std::memory_order_relaxed);
        _dequeuePos.store(0, std::memory_order_relaxed);
    }

    ~MpscRingBuffer() = default;

    MpscRingBuffer(const MpscRingBuffer&) = delete;
    MpscRingBuffer& operator=(const MpscRingBuffer&) = delete;

    struct PushResult {
        bool pushed;
        bool wasEmpty;
    };

    PushResult tryPush(T&& value)
    {
        Cell* cell;
        size_t pos = _enqueuePos.load(std::memory_order_relaxed);

        for (;;) {
            cell = &_buffer[pos & _mask];
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

        cell->data = std::move(value);
        cell->sequence.store(pos + 1, std::memory_order_release);
        bool wasEmpty = (pos == _dequeuePos.load(std::memory_order_acquire));
        return {true, wasEmpty};
    }

    PushResult tryPush(const T& value)
    {
        T copy = value;
        return tryPush(std::move(copy));
    }

    bool tryPop(T& value)
    {
        size_t pos = _dequeuePos.load(std::memory_order_relaxed);
        Cell* cell = &_buffer[pos & _mask];
        size_t seq = cell->sequence.load(std::memory_order_acquire);
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

        if (diff == 0) {
            _dequeuePos.store(pos + 1, std::memory_order_release);
            value = std::move(cell->data);
            cell->sequence.store(pos + _mask + 1, std::memory_order_release);
            return true;
        }

        return false;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return _enqueuePos.load(std::memory_order_relaxed) == _dequeuePos.load(std::memory_order_relaxed);
    }

    [[nodiscard]] constexpr size_t capacity() const noexcept
    {
        return Capacity;
    }

private:
    alignas(CacheLineSize) std::atomic<size_t> _enqueuePos{0};
    alignas(CacheLineSize) std::atomic<size_t> _dequeuePos{0};
    alignas(CacheLineSize) const size_t _mask;
    std::array<Cell, Capacity> _buffer;
};

} // namespace corium
