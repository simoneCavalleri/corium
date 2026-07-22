#pragma once

#include <cstddef>
#include "corium/Events.hpp"
#include "corium/policies/Policies.hpp"

namespace corium {

template <
    typename EventVariant,
    typename QueuePolicy,
    typename SignalPolicy,
    typename StoragePolicy
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
    typename SignalPolicy = NoSignalPolicy,
    typename StoragePolicy = DefaultStoragePolicy
>
struct RuntimeBuilder {
    /// @brief Specify custom event variant list type (preserves existing queue capacity/policy).
    template <typename NewEventVariant>
    using WithEvents = RuntimeBuilder<
        NewEventVariant,
        internal::rebind_queue_policy_t<QueuePolicy, NewEventVariant>,
        SignalPolicy,
        StoragePolicy
    >;

    /// @brief Specify custom queue capacity for bounded MPSC queue (preserves existing event variant).
    template <size_t Capacity>
    using WithCapacity = RuntimeBuilder<
        EventVariant,
        internal::rebind_queue_capacity_t<QueuePolicy, Capacity>,
        SignalPolicy,
        StoragePolicy
    >;

    /// @brief Specify custom QueuePolicy.
    template <typename NewQueuePolicy>
    using WithQueuePolicy = RuntimeBuilder<
        EventVariant,
        NewQueuePolicy,
        SignalPolicy,
        StoragePolicy
    >;

    /// @brief Specify custom SignalPolicy.
    template <typename NewSignalPolicy>
    using WithSignalPolicy = RuntimeBuilder<
        EventVariant,
        QueuePolicy,
        NewSignalPolicy,
        StoragePolicy
    >;

    /// @brief Specify custom StoragePolicy.
    template <typename NewStoragePolicy>
    using WithStoragePolicy = RuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        NewStoragePolicy
    >;

    /// @brief Specify maximum handlers per event type (rebinds StoragePolicy).
    template <size_t NewMaxHandlers>
    using WithMaxHandlersPerEvent = RuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        FixedStoragePolicy<NewMaxHandlers, StoragePolicy::inline_storage_size>
    >;

    /// @brief Specify inline storage size for FastDelegate (rebinds StoragePolicy).
    template <size_t NewInlineSize>
    using WithInlineSize = RuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        FixedStoragePolicy<StoragePolicy::max_handlers_per_event, NewInlineSize>
    >;

    /// @brief Complete builder configuration and return BasicRuntime type.
    using Build = BasicRuntime<EventVariant, QueuePolicy, SignalPolicy, StoragePolicy>;
};

} // namespace corium
