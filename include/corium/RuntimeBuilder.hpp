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

/// @brief Fluent compile-time builder for configuring BasicRuntime type aliases.
template <
    typename EventVariant = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariant, 1024>,
    typename SignalPolicy = CallbackSignalPolicy
>
struct RuntimeBuilder {
    /// @brief Specify custom event variant list type.
    template <typename NewEventVariant>
    using WithEvents = RuntimeBuilder<
        NewEventVariant,
        BoundedMpscQueuePolicy<NewEventVariant, 1024>,
        SignalPolicy
    >;

    /// @brief Specify custom queue capacity for bounded MPSC queue.
    template <size_t Capacity>
    using WithCapacity = RuntimeBuilder<
        EventVariant,
        BoundedMpscQueuePolicy<EventVariant, Capacity>,
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
