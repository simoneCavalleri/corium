#pragma once

#include <cstddef>
#include "corium/Events.hpp"
#include "corium/policies/Policies.hpp"

namespace corium {

template <
    typename EventVariant,
    typename QueuePolicy,
    typename SignalPolicy
>
class BasicRuntime;

namespace internal {

/// @brief Rebind QueuePolicy EventVariant type parameter while preserving Capacity or Policy structure.
template <typename QueuePolicy, typename NewEventVariant>
struct rebind_queue_policy;

template <template <typename, size_t> class QueuePolicy, typename OldEventVariant, size_t Capacity, typename NewEventVariant>
struct rebind_queue_policy<QueuePolicy<OldEventVariant, Capacity>, NewEventVariant> {
    using type = QueuePolicy<NewEventVariant, Capacity>;
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

template <typename QueuePolicy, size_t NewCapacity>
using rebind_queue_capacity_t = typename rebind_queue_capacity<QueuePolicy, NewCapacity>::type;

} // namespace internal

/// @brief Fluent compile-time builder for configuring BasicRuntime type aliases.
template <
    typename EventVariant = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariant, 1024>,
    typename SignalPolicy = CallbackSignalPolicy
>
struct RuntimeBuilder {
    /// @brief Specify custom event variant list type (preserves existing queue capacity/policy).
    template <typename NewEventVariant>
    using WithEvents = RuntimeBuilder<
        NewEventVariant,
        internal::rebind_queue_policy_t<QueuePolicy, NewEventVariant>,
        SignalPolicy
    >;

    /// @brief Specify custom queue capacity for bounded MPSC queue (preserves existing event variant).
    template <size_t Capacity>
    using WithCapacity = RuntimeBuilder<
        EventVariant,
        internal::rebind_queue_capacity_t<QueuePolicy, Capacity>,
        SignalPolicy
    >;

    /// @brief Specify custom QueuePolicy.
    template <typename NewQueuePolicy>
    using WithQueuePolicy = RuntimeBuilder<
        EventVariant,
        NewQueuePolicy,
        SignalPolicy
    >;

    /// @brief Specify custom SignalPolicy.
    template <typename NewSignalPolicy>
    using WithSignalPolicy = RuntimeBuilder<
        EventVariant,
        QueuePolicy,
        NewSignalPolicy
    >;

    /// @brief Complete builder configuration and return BasicRuntime type.
    using Build = BasicRuntime<EventVariant, QueuePolicy, SignalPolicy>;
};

} // namespace corium
