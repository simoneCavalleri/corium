#include <gtest/gtest.h>
#include <corium/corium.hpp>
#include <string>
#include <vector>

using namespace corium;
using namespace corium::fsm;

// States
struct StateIdle {
    static inline std::vector<std::string>* logPtr = nullptr;
    void onEnter() { if (logPtr) logPtr->emplace_back("Idle:Enter"); }
    void onExit() { if (logPtr) logPtr->emplace_back("Idle:Exit"); }
};

struct StateRunning {
    int speed = 0;
    static inline std::vector<std::string>* logPtr = nullptr;
    void onEnter() { if (logPtr) logPtr->emplace_back("Running:Enter"); }
    void onExit() { if (logPtr) logPtr->emplace_back("Running:Exit"); }
};

struct StateFault {
    int errorCode = 0;
    static inline std::vector<std::string>* logPtr = nullptr;
    void onEnter() { if (logPtr) logPtr->emplace_back("Fault:Enter"); }
    void onExit() { if (logPtr) logPtr->emplace_back("Fault:Exit"); }
};

// Events
struct StartCommand { int targetSpeed = 10; };
struct StopCommand {};
struct FaultTrigger { int code = 500; };
struct ResetCommand {};

// Transition Actions & Guards
struct SetSpeedAction {
    void operator()(StateIdle&, const StartCommand& cmd, StateRunning& next) const {
        next.speed = cmd.targetSpeed;
    }
};

struct FaultGuard {
    bool operator()(const StateRunning&, const FaultTrigger& f) const {
        return f.code > 100;
    }
};

struct SetFaultAction {
    void operator()(StateRunning&, const FaultTrigger& f, StateFault& next) const {
        next.errorCode = f.code;
    }
};

// Transition Table
using MotorFsmTable = TransitionTable<
    // Idle -> Running on StartCommand
    Transition<StateIdle, StartCommand, StateRunning, Always, SetSpeedAction>,
    // Running -> Idle on StopCommand
    Transition<StateRunning, StopCommand, StateIdle>,
    // Running -> Fault on FaultTrigger (with guard)
    Transition<StateRunning, FaultTrigger, StateFault, FaultGuard, SetFaultAction>,
    // Fault -> Idle on ResetCommand
    Transition<StateFault, ResetCommand, StateIdle>
>;

using AppEvents = std::variant<QuitEvent, StartCommand, StopCommand>;
using FsmTestRuntime = RuntimeBuilder::WithEvents<AppEvents>::Build;

class FsmIntegrationApp : public Application<FsmIntegrationApp, AppEvents> {
public:
    StateMachine<MotorFsmTable, StateIdle, StateRunning, StateFault> motorFsm;

    void onRegisterHandlers() {
        on([this](const StartCommand& cmd) {
            motorFsm.process_event(cmd);
        });
        on([this](const StopCommand& cmd) {
            motorFsm.process_event(cmd);
        });
    }
};

TEST(FsmTest, BasicStateTransitionsAndLifecycle)
{
    std::vector<std::string> log;
    StateIdle::logPtr = &log;
    StateRunning::logPtr = &log;
    StateFault::logPtr = &log;

    StateMachine<MotorFsmTable, StateIdle, StateRunning, StateFault> fsm;
    EXPECT_TRUE(fsm.is<StateIdle>());
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "Idle:Enter");

    // Idle -> Running
    EXPECT_TRUE(fsm.process_event(StartCommand{120}));
    EXPECT_TRUE(fsm.is<StateRunning>());
    EXPECT_EQ(fsm.as<StateRunning>().speed, 120);
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[1], "Idle:Exit");
    EXPECT_EQ(log[2], "Running:Enter");

    // Running -> Stop -> Idle
    EXPECT_TRUE(fsm.process_event(StopCommand{}));
    EXPECT_TRUE(fsm.is<StateIdle>());
    ASSERT_EQ(log.size(), 5u);
    EXPECT_EQ(log[3], "Running:Exit");
    EXPECT_EQ(log[4], "Idle:Enter");

    StateIdle::logPtr = nullptr;
    StateRunning::logPtr = nullptr;
    StateFault::logPtr = nullptr;
}

TEST(FsmTest, GuardEvaluation)
{
    StateMachine<MotorFsmTable, StateIdle, StateRunning, StateFault> fsm;
    fsm.process_event(StartCommand{50});
    EXPECT_TRUE(fsm.is<StateRunning>());

    // Guard rejects code <= 100
    EXPECT_FALSE(fsm.process_event(FaultTrigger{50}));
    EXPECT_TRUE(fsm.is<StateRunning>()); // remains Running

    // Guard accepts code > 100
    EXPECT_TRUE(fsm.process_event(FaultTrigger{404}));
    EXPECT_TRUE(fsm.is<StateFault>());
    EXPECT_EQ(fsm.as<StateFault>().errorCode, 404);

    // Reset back to Idle
    EXPECT_TRUE(fsm.process_event(ResetCommand{}));
    EXPECT_TRUE(fsm.is<StateIdle>());
}

TEST(FsmTest, FsmApplicationIntegration)
{
    FsmTestRuntime runtime;
    FsmIntegrationApp app;
    runtime.initialize(app);

    EXPECT_TRUE(app.motorFsm.is<StateIdle>());

    runtime.eventSink().post(StartCommand{80});
    runtime.pump();
    EXPECT_TRUE(app.motorFsm.is<StateRunning>());
    EXPECT_EQ(app.motorFsm.as<StateRunning>().speed, 80);

    runtime.eventSink().post(StopCommand{});
    runtime.pump();
    EXPECT_TRUE(app.motorFsm.is<StateIdle>());

    runtime.shutdown();
}

// -----------------------------------------------------------------------------
// Internal Transition & ActionList Tests
// -----------------------------------------------------------------------------
struct SetSpeedInternalAction {
    void operator()(StateRunning& state, const StartCommand& cmd) const {
        state.speed = cmd.targetSpeed;
    }
};

struct IncrementLogAction {
    int* counter = nullptr;
    void operator()(StateRunning&, const StartCommand&) const {
        if (counter) (*counter)++;
    }
};

using AdvancedFsmTable = TransitionTable<
    Transition<StateIdle, StartCommand, StateRunning, Always, SetSpeedAction>,
    InternalTransition<StateRunning, StartCommand, Always, ActionList<SetSpeedInternalAction, IncrementLogAction>>
>;

TEST(FsmTest, InternalTransitionDoesNotTriggerExitOrEnter)
{
    std::vector<std::string> log;
    StateIdle::logPtr = &log;
    StateRunning::logPtr = &log;

    int actionListCounter = 0;
    IncrementLogAction inc{&actionListCounter};

    using TableWithCustomAction = TransitionTable<
        Transition<StateIdle, StartCommand, StateRunning, Always, SetSpeedAction>,
        InternalTransition<StateRunning, StartCommand, Always, ActionList<SetSpeedInternalAction, IncrementLogAction>>
    >;

    StateMachine<TableWithCustomAction, StateIdle, StateRunning> fsm;
    EXPECT_TRUE(fsm.is<StateIdle>());
    ASSERT_EQ(log.size(), 1u); // Idle:Enter

    // Idle -> Running (External transition: Exit Idle, Enter Running)
    EXPECT_TRUE(fsm.process_event(StartCommand{50}));
    EXPECT_TRUE(fsm.is<StateRunning>());
    EXPECT_EQ(fsm.as<StateRunning>().speed, 50);
    ASSERT_EQ(log.size(), 3u); // Idle:Exit, Running:Enter

    // Internal transition while in StateRunning: updates speed WITHOUT calling onExit or onEnter
    EXPECT_TRUE(fsm.process_event(StartCommand{100}));
    EXPECT_TRUE(fsm.is<StateRunning>());
    EXPECT_EQ(fsm.as<StateRunning>().speed, 100);
    EXPECT_EQ(log.size(), 3u); // NO new onExit or onEnter entries!

    StateIdle::logPtr = nullptr;
    StateRunning::logPtr = nullptr;
}
