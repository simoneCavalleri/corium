#pragma once

#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "corium/fsm/Transition.hpp"

namespace corium::fsm {

namespace detail {

template <typename State, typename Event>
constexpr void call_on_exit(State& s, const Event& e) {
    if constexpr (requires { s.onExit(e); }) {
        s.onExit(e);
    } else if constexpr (requires { s.onExit(); }) {
        s.onExit();
    }
}

template <typename State, typename Event>
constexpr void call_on_enter(State& s, const Event& e) {
    if constexpr (requires { s.onEnter(e); }) {
        s.onEnter(e);
    } else if constexpr (requires { s.onEnter(); }) {
        s.onEnter();
    }
}

template <typename Guard, typename State, typename Event>
constexpr bool evaluate_guard(const Guard& guard, const State& s, const Event& e) {
    if constexpr (std::is_invocable_r_v<bool, Guard, const State&, const Event&>) {
        return guard(s, e);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const Event&>) {
        return guard(e);
    } else if constexpr (std::is_invocable_r_v<bool, Guard>) {
        return guard();
    } else {
        return true;
    }
}

template <typename Action, typename From, typename Event, typename To>
constexpr void execute_action(const Action& action, From& from, const Event& e, To& to) {
    if constexpr (std::is_invocable_v<Action, From&, const Event&, To&>) {
        action(from, e, to);
    } else if constexpr (std::is_invocable_v<Action, From&, const Event&>) {
        action(from, e);
    } else if constexpr (std::is_invocable_v<Action, const Event&>) {
        action(e);
    } else if constexpr (std::is_invocable_v<Action>) {
        action();
    }
}

} // namespace detail

/// @ingroup fsm
/// @brief Zero-heap, compile-time Finite State Machine.
/// @tparam Table TransitionTable defining valid state transitions.
/// @tparam InitialState Default initial active state.
/// @tparam OtherStates Additional valid states in the FSM state set.
template <
    typename Table,
    typename InitialState,
    typename... OtherStates
>
class StateMachine {
public:
    using StateVariant = std::variant<InitialState, OtherStates...>;
    using TransitionTableType = Table;

    constexpr StateMachine()
        : _state(InitialState{})
    {
        detail::call_on_enter(std::get<InitialState>(_state), int{0});
    }

    template <typename State>
        requires (std::is_constructible_v<StateVariant, State>)
    constexpr explicit StateMachine(State&& initial)
        : _state(std::forward<State>(initial))
    {
        std::visit([](auto& s) {
            detail::call_on_enter(s, int{0});
        }, _state);
    }

    /// @brief Check if current active state matches type State.
    template <typename State>
    [[nodiscard]] constexpr bool is() const noexcept {
        return std::holds_alternative<State>(_state);
    }

    /// @brief Access reference to current state as type State (throws std::bad_variant_access if mismatch).
    template <typename State>
    [[nodiscard]] constexpr State& as() {
        return std::get<State>(_state);
    }

    /// @brief Access const reference to current state as type State.
    template <typename State>
    [[nodiscard]] constexpr const State& as() const {
        return std::get<State>(_state);
    }

    /// @brief Access active state variant.
    [[nodiscard]] constexpr const StateVariant& state() const noexcept {
        return _state;
    }

    /// @brief Access active state variant.
    [[nodiscard]] constexpr StateVariant& state() noexcept {
        return _state;
    }

    /// @brief Process an incoming event through the state machine transition table.
    /// @tparam Event Trigger event type.
    /// @param event Event instance to evaluate.
    /// @return true if a transition was matched and executed; false if no transition applied.
    template <typename Event>
    bool process_event(const Event& event) {
        return std::visit([this, &event](auto& currentState) -> bool {
            using CurrentStateType = std::decay_t<decltype(currentState)>;
            return this->try_transition<CurrentStateType, Event>(currentState, event, Table{});
        }, _state);
    }

private:
    template <typename CurrentState, typename Event, typename... Transitions>
    bool try_transition(CurrentState& current, const Event& event, TransitionTable<Transitions...>) {
        return (try_single_transition<CurrentState, Event, Transitions>(current, event, Transitions{}) || ...);
    }

    template <typename CurrentState, typename Event, typename Trans>
    bool try_single_transition(CurrentState& current, const Event& event, const Trans& trans) {
        if constexpr (std::is_same_v<CurrentState, typename Trans::FromState> &&
                      std::is_same_v<std::decay_t<Event>, typename Trans::EventType>) {
            if (detail::evaluate_guard(trans.guard, current, event)) {
                detail::call_on_exit(current, event);
                typename Trans::ToState nextState{};
                detail::execute_action(trans.action, current, event, nextState);
                _state = std::move(nextState);
                detail::call_on_enter(std::get<typename Trans::ToState>(_state), event);
                return true;
            }
        }
        return false;
    }

    StateVariant _state;
};

} // namespace corium::fsm
