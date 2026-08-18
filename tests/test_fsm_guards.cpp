#include <gtest/gtest.h>
#include <string>

#include "corium/corium.hpp"

namespace {

// States
struct IdleState {};
struct RunningState {};
struct FaultState {};

// Events
struct StartCommand {
    int speed;
    bool safetyCheckPassed;
};

struct StopCommand {};

// Guards
struct SafetyCheckGuard {
    bool operator()(const IdleState&, const StartCommand& cmd) const noexcept {
        return cmd.safetyCheckPassed && cmd.speed > 0;
    }
};

struct SpeedLimitGuard {
    bool operator()(const RunningState&, const StopCommand&) const noexcept {
        return true;
    }
};

using MotorTransitions = corium::fsm::TransitionTable<
    corium::fsm::Transition<IdleState, StartCommand, RunningState, SafetyCheckGuard>,
    corium::fsm::Transition<RunningState, StopCommand, IdleState, SpeedLimitGuard>
>;

using MotorFsm = corium::fsm::StateMachine<MotorTransitions, IdleState, RunningState, FaultState>;

} // namespace

TEST(FsmGuardsTest, GuardPreventsTransitionWhenFalse) {
    MotorFsm fsm;

    EXPECT_TRUE(fsm.is<IdleState>());

    // 1. Safety check false -> should NOT transition to RunningState
    bool handled = fsm.process_event(StartCommand{.speed = 100, .safetyCheckPassed = false});
    EXPECT_FALSE(handled);
    EXPECT_TRUE(fsm.is<IdleState>());

    // 2. Safety check true but speed <= 0 -> should NOT transition
    handled = fsm.process_event(StartCommand{.speed = 0, .safetyCheckPassed = true});
    EXPECT_FALSE(handled);
    EXPECT_TRUE(fsm.is<IdleState>());

    // 3. Safety check true and speed > 0 -> should transition to RunningState
    handled = fsm.process_event(StartCommand{.speed = 150, .safetyCheckPassed = true});
    EXPECT_TRUE(handled);
    EXPECT_TRUE(fsm.is<RunningState>());

    // 4. Stop command -> should transition back to IdleState
    handled = fsm.process_event(StopCommand{});
    EXPECT_TRUE(handled);
    EXPECT_TRUE(fsm.is<IdleState>());
}
