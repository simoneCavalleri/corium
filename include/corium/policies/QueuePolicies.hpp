#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>
#include <type_traits>
#include <utility>

#include "corium/Events.hpp"
#include "corium/internal/MpscRingBuffer.hpp"

namespace corium {

/// @brief Event priority levels for multi-priority queue policies.
enum class EventPriority : uint8_t {
    High = 0,
    Normal = 1,
    Low = 2
};

/// @brief Queue Policy for fixed-capacity, zero-allocation lock-free MPSC RingBuffer.
/// @tparam EventVariant The variant type list of supported events.
/// @tparam Capacity Ring buffer capacity (must be a power of 2).
template <typename EventVariant = DefaultEvents, size_t Capacity = 1024>
class BoundedMpscQueuePolicy {
public:
    using EventType = EventVariant;
    static constexpr std::size_t capacity = Capacity; ///< Accessible queue capacity for profiler wiring.

    struct PushResult {
        bool pushed;
        bool wasEmpty;
    };

    /// @brief Try to push an event into the lock-free queue (thread-safe, zero allocations).
    PushResult tryPush(EventVariant&& event, EventPriority priority = EventPriority::Normal)
    {
        (void)priority;
        auto res = _ringBuffer.tryPush(std::move(event));
        return {res.pushed, res.wasEmpty};
    }

    /// @brief Try to push an event into the lock-free queue (const lvalue overload).
    PushResult tryPush(const EventVariant& event, EventPriority priority = EventPriority::Normal)
    {
        EventVariant copy = event;
        return tryPush(std::move(copy), priority);
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

/// @brief Queue Policy supporting strict event priorities using separate lock-free MPSC ring buffers.
/// High priority events are always popped and dispatched before Normal/Low priority events.
/// @tparam EventVariant The variant type list of supported events.
/// @tparam HighCapacity Capacity for high-priority ring buffer (power of 2).
/// @tparam NormalCapacity Capacity for normal-priority ring buffer (power of 2).
/// @tparam LowCapacity Capacity for low-priority ring buffer (power of 2, 0 to disable).
template <
    typename EventVariant = DefaultEvents,
    size_t HighCapacity = 256,
    size_t NormalCapacity = 1024,
    size_t LowCapacity = 0
>
class PriorityMpscQueuePolicy {
public:
    using EventType = EventVariant;
    /// @brief Total accessible capacity (high + normal; low is optional).
    static constexpr std::size_t capacity = HighCapacity + NormalCapacity + (LowCapacity > 0 ? LowCapacity : 0);

    struct PushResult {
        bool pushed;
        bool wasEmpty;
    };

    /// @brief Try to push an event into the priority queue (thread-safe, zero dynamic allocation).
    PushResult tryPush(EventVariant&& event, EventPriority priority = EventPriority::Normal)
    {
        bool wasOverallEmpty = empty();
        bool pushed = false;

        switch (priority) {
            case EventPriority::High: {
                auto res = _highRingBuffer.tryPush(std::move(event));
                pushed = res.pushed;
                break;
            }
            case EventPriority::Normal: {
                auto res = _normalRingBuffer.tryPush(std::move(event));
                pushed = res.pushed;
                break;
            }
            case EventPriority::Low: {
                if constexpr (LowCapacity > 0) {
                    auto res = _lowRingBuffer.tryPush(std::move(event));
                    pushed = res.pushed;
                } else {
                    auto res = _normalRingBuffer.tryPush(std::move(event));
                    pushed = res.pushed;
                }
                break;
            }
        }

        return {pushed, wasOverallEmpty};
    }

    /// @brief Try to push an event into the priority queue (const lvalue overload).
    PushResult tryPush(const EventVariant& event, EventPriority priority = EventPriority::Normal)
    {
        EventVariant copy = event;
        return tryPush(std::move(copy), priority);
    }

    /// @brief Try to pop an event from the priority queue (strict priority order: High -> Normal -> Low).
    bool tryPop(EventVariant& event)
    {
        if (_highRingBuffer.tryPop(event)) {
            return true;
        }
        if (_normalRingBuffer.tryPop(event)) {
            return true;
        }
        if constexpr (LowCapacity > 0) {
            if (_lowRingBuffer.tryPop(event)) {
                return true;
            }
        }
        return false;
    }

    /// @brief Check if all priority queues are empty.
    [[nodiscard]] bool empty() const noexcept
    {
        bool isEmpty = _highRingBuffer.empty() && _normalRingBuffer.empty();
        if constexpr (LowCapacity > 0) {
            isEmpty = isEmpty && _lowRingBuffer.empty();
        }
        return isEmpty;
    }

private:
    MpscRingBuffer<EventVariant, HighCapacity> _highRingBuffer;
    MpscRingBuffer<EventVariant, NormalCapacity> _normalRingBuffer;
    [[no_unique_address]] std::conditional_t<(LowCapacity > 0),
        MpscRingBuffer<EventVariant, (LowCapacity > 0 ? LowCapacity : 1)>,
        std::monostate
    > _lowRingBuffer{};
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
    PushResult tryPush(EventVariant event, EventPriority priority = EventPriority::Normal)
    {
        (void)priority;
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

/// @brief Zero-overhead Queue Policy for services or buses that do not receive or queue incoming events.
/// Occupies zero storage space when combined with [[no_unique_address]] in container types.
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
class NoQueuePolicy {
public:
    using EventType = EventVariant;

    struct PushResult {
        bool pushed = false;
        bool wasEmpty = false;
    };

    /// @brief Always fails to push events as queue capacity is 0.
    PushResult tryPush(EventVariant, EventPriority = EventPriority::Normal) noexcept
    {
        return {false, false};
    }

    /// @brief Always fails to pop events as queue capacity is 0.
    bool tryPop(EventVariant&) noexcept
    {
        return false;
    }

    /// @brief Always returns true as no events can be queued.
    [[nodiscard]] bool empty() const noexcept
    {
        return true;
    }
};

} // namespace corium

