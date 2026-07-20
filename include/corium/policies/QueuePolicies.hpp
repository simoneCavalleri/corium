#pragma once

#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>

#include "corium/Events.hpp"
#include "corium/internal/MpscRingBuffer.hpp"

namespace corium {

/// @brief Queue Policy for fixed-capacity, zero-allocation lock-free MPSC RingBuffer.
/// @tparam EventVariant The variant type list of supported events.
/// @tparam Capacity Ring buffer capacity (must be a power of 2).
template <typename EventVariant = DefaultEvents, size_t Capacity = 1024>
class BoundedMpscQueuePolicy {
public:
    using EventType = EventVariant;

    struct PushResult {
        bool pushed;
        bool wasEmpty;
    };

    /// @brief Try to push an event into the lock-free queue (thread-safe, zero allocations).
    PushResult tryPush(EventVariant event)
    {
        auto res = _ringBuffer.tryPush(std::move(event));
        return {res.pushed, res.wasEmpty};
    }

    /// @brief Try to pop an event from the lock-free queue (single consumer).
    bool tryPop(EventVariant& event)
    {
        return _ringBuffer.tryPop(event);
    }

    /// @brief Check if queue is empty.
    [[nodiscard]] bool empty() const noexcept
    {
        return _ringBuffer.empty();
    }

private:
    MpscRingBuffer<EventVariant, Capacity> _ringBuffer;
};

/// @brief Queue Policy for a traditional mutex-protected blocking queue (power saving).
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
class BlockingQueuePolicy {
public:
    using EventType = EventVariant;

    struct PushResult {
        bool pushed;
        bool wasEmpty;
    };

    /// @brief Try to push an event into the blocking queue.
    PushResult tryPush(EventVariant event)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        bool wasEmpty = _queue.empty();
        _queue.push(std::move(event));
        return {true, wasEmpty};
    }

    /// @brief Try to pop an event from the blocking queue.
    bool tryPop(EventVariant& event)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_queue.empty()) {
            return false;
        }
        event = std::move(_queue.front());
        _queue.pop();
        return true;
    }

    /// @brief Check if queue is empty.
    [[nodiscard]] bool empty() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.empty();
    }

private:
    std::queue<EventVariant> _queue;
    mutable std::mutex _mutex;
};

} // namespace corium
