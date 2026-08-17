#pragma once

#include <type_traits>
#include <utility>

namespace corium::fsm {

/// @brief Default always-true guard for state machine transitions.
struct Always {
    template <typename... Args>
    constexpr bool operator()(Args&&...) const noexcept { return true; }
};

/// @brief Default no-op action for state machine transitions.
struct NoAction {
    template <typename... Args>
    constexpr void operator()(Args&&...) const noexcept {}
};

/// @brief Compile-time transition rule definition.
/// @tparam From Source state type.
/// @tparam Event Trigger event type.
/// @tparam To Destination state type.
/// @tparam Guard Callable predicate (FromState&, const Event&) -> bool.
/// @tparam Action Callable action (FromState&, const Event&, ToState&) -> void.
template <
    typename From,
    typename Event,
    typename To,
    typename Guard = Always,
    typename Action = NoAction
>
struct Transition {
    using FromState = From;
    using EventType = Event;
    using ToState = To;
    using GuardType = Guard;
    using ActionType = Action;

    [[no_unique_address]] GuardType guard{};
    [[no_unique_address]] ActionType action{};

    constexpr Transition() = default;
    constexpr explicit Transition(GuardType g, ActionType a = ActionType{})
        : guard(std::move(g)), action(std::move(a))
    {}
};

/// @brief Compile-time table containing all valid state transitions.
template <typename... Transitions>
struct TransitionTable {
    using TransitionsTuple = std::tuple<Transitions...>;
};

} // namespace corium::fsm
