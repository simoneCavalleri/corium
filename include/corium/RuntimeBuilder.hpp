#pragma once

#include <cstddef>
#include "corium/Runtime.hpp"
#include "corium/policies/OverflowPolicies.hpp"
#include "corium/policies/QueuePolicies.hpp"
#include "corium/policies/SignalPolicies.hpp"
#include "corium/policies/StoragePolicies.hpp"
#include "corium/policies/TimerPolicies.hpp"
#include "corium/profiler/ProfilerPolicies.hpp"

namespace corium {

template <
    typename EventVariant,
    typename QueuePolicy,
    typename SignalPolicy,
    typename StoragePolicy,
    typename OverflowPolicy,
    typename TimerStoragePolicy,
    typename ProfilerPolicy
>
class BasicRuntime;

namespace internal {

/// @brief Compile-time helper to round up an integer to the next power of 2.
template <std::size_t N>
constexpr std::size_t next_power_of_two() {
    if (N <= 1) return 1;
    std::size_t val = N - 1;
    val |= val >> 1;
    val |= val >> 2;
    val |= val >> 4;
    val |= val >> 8;
    val |= val >> 16;
    val |= val >> 32;
    return val + 1;
}

/// @brief Extract queue capacity from a QueuePolicy if it exposes a static ::capacity member,
/// rounded up to the nearest power of 2 for power-of-two ring buffer constraints.
/// Falls back to 1024 for policies without a capacity (e.g. NoQueuePolicy).
template <typename QueuePolicy, typename = void>
struct queue_capacity_of : std::integral_constant<std::size_t, 1024> {};

template <typename QueuePolicy>
struct queue_capacity_of<QueuePolicy, std::void_t<decltype(QueuePolicy::capacity)>>
    : std::integral_constant<std::size_t, next_power_of_two<QueuePolicy::capacity>()> {};

template <typename QueuePolicy>
static constexpr std::size_t queue_capacity_of_v = queue_capacity_of<QueuePolicy>::value;

/// @brief Rebind QueuePolicy EventVariant type parameter while preserving Capacity or Policy structure.
template <typename QueuePolicy, typename NewEventVariant>
struct rebind_queue_policy;

template <template <typename, size_t> class QueuePolicy, typename OldEventVariant, size_t Capacity, typename NewEventVariant>
struct rebind_queue_policy<QueuePolicy<OldEventVariant, Capacity>, NewEventVariant> {
    using type = QueuePolicy<NewEventVariant, Capacity>;
};

template <template <typename, size_t, size_t, size_t> class QueuePolicy, typename OldEventVariant, size_t HighCap, size_t NormalCap, size_t LowCap, typename NewEventVariant>
struct rebind_queue_policy<QueuePolicy<OldEventVariant, HighCap, NormalCap, LowCap>, NewEventVariant> {
    using type = QueuePolicy<NewEventVariant, HighCap, NormalCap, LowCap>;
};

template <template <typename> class QueuePolicy, typename OldEventVariant, typename NewEventVariant>
struct rebind_queue_policy<QueuePolicy<OldEventVariant>, NewEventVariant> {
    using type = QueuePolicy<NewEventVariant>;
};

template <typename QueuePolicy, typename NewEventVariant>
using rebind_queue_policy_t = typename rebind_queue_policy<QueuePolicy, NewEventVariant>::type;

/// @brief Rebind QueuePolicy Capacity while preserving EventVariant.
template <typename QueuePolicy, size_t NewCapacity>
struct rebind_queue_capacity;

template <template <typename, size_t> class QueuePolicy, typename EventVariant, size_t OldCapacity, size_t NewCapacity>
struct rebind_queue_capacity<QueuePolicy<EventVariant, OldCapacity>, NewCapacity> {
    using type = QueuePolicy<EventVariant, NewCapacity>;
};

template <template <typename, size_t, size_t, size_t> class QueuePolicy, typename EventVariant, size_t HighCap, size_t OldNormalCap, size_t LowCap, size_t NewCapacity>
struct rebind_queue_capacity<QueuePolicy<EventVariant, HighCap, OldNormalCap, LowCap>, NewCapacity> {
    using type = QueuePolicy<EventVariant, HighCap, NewCapacity, LowCap>;
};

template <typename QueuePolicy, size_t NewCapacity>
using rebind_queue_capacity_t = typename rebind_queue_capacity<QueuePolicy, NewCapacity>::type;

} // namespace internal

/// @ingroup core
/// @brief Primary template implementation for fluent compile-time Runtime construction.
template <
    typename EventVariant = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariant, 1024>,
    typename SignalPolicy = NoSignalPolicy,
    typename StoragePolicy = DefaultStoragePolicy,
    typename OverflowPolicy = DropNewestOverflowPolicy,
    typename TimerStoragePolicy = DefaultTimerStoragePolicy,
    typename ProfilerPolicy = profiler::NullProfiler
>
struct BasicRuntimeBuilder {
    /// @brief Specify custom event variant list type (preserves existing queue capacity/policy).
    template <typename NewEventVariant>
    using WithEvents = BasicRuntimeBuilder<
        NewEventVariant,
        internal::rebind_queue_policy_t<QueuePolicy, NewEventVariant>,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify custom queue capacity for bounded MPSC queue (preserves existing event variant).
    template <size_t Capacity>
    using WithCapacity = BasicRuntimeBuilder<
        EventVariant,
        internal::rebind_queue_capacity_t<QueuePolicy, Capacity>,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Switch queue policy to PriorityMpscQueuePolicy with specified high and normal capacities.
    template <size_t HighCapacity = 256, size_t NormalCapacity = 1024>
    using WithPriorityQueue = BasicRuntimeBuilder<
        EventVariant,
        PriorityMpscQueuePolicy<EventVariant, HighCapacity, NormalCapacity>,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify custom QueuePolicy.
    template <typename NewQueuePolicy>
    using WithQueuePolicy = BasicRuntimeBuilder<
        EventVariant,
        NewQueuePolicy,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify custom SignalPolicy.
    template <typename NewSignalPolicy>
    using WithSignalPolicy = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        NewSignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify custom StoragePolicy.
    template <typename NewStoragePolicy>
    using WithStoragePolicy = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        NewStoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify custom OverflowPolicy.
    template <typename NewOverflowPolicy>
    using WithOverflowPolicy = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        StoragePolicy,
        NewOverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify maximum number of concurrent active timers.
    template <size_t MaxTimers>
    using WithMaxTimers = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        FixedTimerStoragePolicy<MaxTimers>,
        ProfilerPolicy
    >;

    /// @brief Specify custom ClockPolicy (time source strategy).
    template <typename NewClockPolicy>
    using WithClockPolicy = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        FixedTimerStoragePolicy<TimerStoragePolicy::max_timers, NewClockPolicy>,
        ProfilerPolicy
    >;

    /// @brief Specify custom TimerStoragePolicy.
    template <typename NewTimerStoragePolicy>
    using WithTimerStoragePolicy = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        NewTimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify maximum handlers per event type (rebinds StoragePolicy).
    template <size_t NewMaxHandlers>
    using WithMaxHandlersPerEvent = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        FixedStoragePolicy<NewMaxHandlers, StoragePolicy::inline_storage_size>,
        OverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify inline storage size for FastDelegate (rebinds StoragePolicy).
    template <size_t NewInlineSize>
    using WithInlineSize = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        FixedStoragePolicy<StoragePolicy::max_handlers_per_event, NewInlineSize>,
        OverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify custom ProfilerPolicy strategy.
    template <typename NewProfilerPolicy>
    using WithProfiler = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        NewProfilerPolicy
    >;

    /// @brief Convenience helper: Configure FlightRecorder circular trace logger.
    /// The QueueCapacity of the internal timestamp ring buffer is automatically derived from
    /// the current QueuePolicy capacity, ensuring accurate per-event latency measurement.
    template <std::size_t BufferCapacity = 256>
    using WithFlightRecorder = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        profiler::FlightRecorderProfiler<BufferCapacity, internal::queue_capacity_of_v<QueuePolicy>>
    >;

    /// @brief Convenience helper: Configure real-time event latency statistics tracker.
    /// The QueueCapacity of the internal timestamp ring buffer is automatically derived from
    /// the current QueuePolicy capacity, ensuring accurate per-event latency measurement.
    template <std::size_t BufferCapacity = 256>
    using WithLatencyTracker = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        profiler::LatencyTracker<internal::queue_capacity_of_v<QueuePolicy>>
    >;

    /// @brief Complete builder configuration and return BasicRuntime type.
    using Build = BasicRuntime<EventVariant, QueuePolicy, SignalPolicy, StoragePolicy, OverflowPolicy, TimerStoragePolicy, ProfilerPolicy>;
};

/// @ingroup core
/// @brief Fluent compile-time builder for configuring BasicRuntime.
/// Usage: `using MyRuntime = corium::RuntimeBuilder::WithEvents<MyEvents>::Build;`
struct RuntimeBuilder : BasicRuntimeBuilder<> {};

} // namespace corium
