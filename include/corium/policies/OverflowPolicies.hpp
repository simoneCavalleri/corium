/**
 * @file OverflowPolicies.hpp
 * @ingroup policies
 * @brief Queue saturation policies (DropNewest, DropOldest, Audit, Panic).
 */

#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <exception>
#include <utility>

#include "corium/policies/QueuePolicies.hpp"

namespace corium {

/// @brief Default Overflow Policy: Silently drop incoming new event when queue is full. Zero overhead.
struct DropNewestOverflowPolicy {
    template <typename QueuePolicy, typename EventVariant>
    bool handleOverflow(QueuePolicy& queue, EventVariant&& event, EventPriority priority = EventPriority::Normal)
    {
        (void)queue;
        (void)event;
        (void)priority;
        return false;
    }
};

/// @brief Overflow Policy: Evict oldest event in queue to make room for the new event.
/// Ideal for high-frequency telemetry and sensor data where newest reading is critical.
struct DropOldestOverflowPolicy {
    template <typename QueuePolicy, typename EventVariant>
    bool handleOverflow(QueuePolicy& queue, EventVariant&& event, EventPriority priority = EventPriority::Normal)
    {
        typename QueuePolicy::EventType dummy;
        queue.tryPop(dummy); // Evict oldest event
        auto res = queue.tryPush(std::forward<EventVariant>(event), priority);
        return res.pushed;
    }
};

/// @brief Overflow Policy: Trigger assertion / panic when queue fills up.
/// Essential for mission-critical systems where event loss is unacceptable.
struct PanicOverflowPolicy {
    template <typename QueuePolicy, typename EventVariant>
    bool handleOverflow(QueuePolicy& queue, EventVariant&& event, EventPriority priority = EventPriority::Normal)
    {
        (void)queue;
        (void)event;
        (void)priority;
#if defined(CORIUM_PANIC_ON_OVERFLOW)
        std::terminate();
#else
        assert(false && "Corium Queue Overflow: Queue capacity exceeded!");
        return false;
#endif
    }
};

/// @brief Overflow Policy: Audit and count dropped events via atomic counter.
class AuditOverflowPolicy {
public:
    template <typename QueuePolicy, typename EventVariant>
    bool handleOverflow(QueuePolicy& queue, EventVariant&& event, EventPriority priority = EventPriority::Normal)
    {
        (void)queue;
        (void)event;
        (void)priority;
        _droppedCount.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    /// @brief Get total number of dropped events.
    [[nodiscard]] uint64_t overflowCount() const noexcept
    {
        return _droppedCount.load(std::memory_order_relaxed);
    }

    /// @brief Reset total dropped event counter to 0.
    void resetOverflowCount() noexcept
    {
        _droppedCount.store(0, std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t> _droppedCount{0};
};

} // namespace corium
