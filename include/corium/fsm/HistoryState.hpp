/**
 * @file HistoryState.hpp
 * @ingroup fsm
 * @brief Tag type for shallow history pseudostate in hierarchical state machines.
 */

#pragma once

namespace corium::fsm {

/// @ingroup fsm
/// @brief Tag type for designating a shallow history pseudostate in hierarchical state machines.
/// When entered, the state machine transitions to the most recently visited state in the group,
/// or to DefaultState if the group has not been visited yet.
/// @tparam DefaultState Fallback state if no history has been recorded.
template <typename DefaultState>
struct ShallowHistory {
    using DefaultStateType = DefaultState;
};

} // namespace corium::fsm
