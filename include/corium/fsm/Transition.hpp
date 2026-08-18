/**
 * @file Transition.hpp
 * @ingroup fsm
 * @brief Declarative compile-time transition rules, internal transitions, and action lists.
 */

#pragma once

#include <tuple>
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
    static constexpr bool is_internal = false;

    [[no_unique_address]] GuardType guard{};
    [[no_unique_address]] ActionType action{};

    constexpr Transition() = default;
    constexpr explicit Transition(GuardType g, ActionType a = ActionType{})
        : guard(std::move(g)), action(std::move(a))
    {}
};

/// @brief Compile-time internal transition rule (executes action without exiting or re-entering state).
template <
    typename State,
    typename Event,
    typename Guard = Always,
    typename Action = NoAction
>
struct InternalTransition {
    using FromState = State;
    using EventType = Event;
    using ToState = State;
    using GuardType = Guard;
    using ActionType = Action;
    static constexpr bool is_internal = true;

    [[no_unique_address]] GuardType guard{};
    [[no_unique_address]] ActionType action{};

    constexpr InternalTransition() = default;
    constexpr explicit InternalTransition(GuardType g, ActionType a = ActionType{})
        : guard(std::move(g)), action(std::move(a))
    {}
};

namespace detail {
template <typename Action, typename From, typename Event, typename To>
constexpr void execute_action(const Action& action, From& from, const Event& e, To& to);
}

/// @brief Sequential composition of multiple transition actions.
template <typename... Actions>
struct ActionList {
    std::tuple<Actions...> actions{};

    constexpr ActionList() = default;
    constexpr explicit ActionList(Actions... a) : actions(std::move(a)...) {}

    template <typename From, typename Event, typename To>
    constexpr void operator()(From& from, const Event& event, To& to) const {
        std::apply([&](const auto&... act) {
            (detail::execute_action(act, from, event, to), ...);
        }, actions);
    }
};

/// @brief Compile-time table containing all valid state transitions.
template <typename... Transitions>
struct TransitionTable {
    using TransitionsTuple = std::tuple<Transitions...>;
};

} // namespace corium::fsm
