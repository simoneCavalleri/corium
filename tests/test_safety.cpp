#include <gtest/gtest.h>

#include "corium/Application.hpp"
#include "corium/Runtime.hpp"
#include "corium/safety/CircuitBreaker.hpp"
#include "corium/safety/HeartbeatMonitor.hpp"
#include "corium/safety/SafetyEvents.hpp"
#include "corium/safety/WatchdogSupervisor.hpp"
#include "corium/safety/WatchdogService.hpp"

#include <chrono>
#include <thread>

using namespace corium;
using namespace corium::safety;

namespace {

using SafetyTestEvents = std::variant<
    QuitEvent,
    WatchdogTimeoutEvent,
    DeadlineMissedEvent,
    CircuitBreakerTrippedEvent
>;

using WdServiceEvents = std::variant<QuitEvent, WatchdogTimeoutEvent>;

struct MonitoredApp : public corium::Application<MonitoredApp, WdServiceEvents> {
    WatchdogService<WdServiceEvents, 4> wdService{std::chrono::milliseconds(10)};
    bool timeoutTriggered = false;
    uint32_t timedOutServiceId = 0;

    template <typename Registry>
    void onConfigureServices(Registry& registry) {
        registry.registerService(wdService);
    }

    void onRegisterHandlers() {
        on([this](const WatchdogTimeoutEvent& e) {
            timeoutTriggered = true;
            timedOutServiceId = e.serviceId;
            requestQuit();
        });
    }
};

} // namespace

TEST(SafetyTest, HeartbeatMonitorMultiServiceTracking)
{
    HeartbeatMonitor<4> monitor;
    EXPECT_EQ(monitor.maxServices(), 4u);

    // Register Service 1 (timeout = 100ms = 100,000,000 ns)
    EXPECT_TRUE(monitor.registerService(1, 100'000'000, 1000));
    // Register Service 2 (timeout = 50ms = 50,000,000 ns)
    EXPECT_TRUE(monitor.registerService(2, 50'000'000, 1000));

    uint32_t failedId = 0;
    uint64_t lastBeat = 0;
    uint64_t limit = 0;

    // Time at 20ms: both healthy
    EXPECT_TRUE(monitor.checkHealth(20'000'000, failedId, lastBeat, limit));

    // Beat service 2 at 40ms
    monitor.beat(2, 40'000'000);

    // Time at 60ms: Service 1 was baselined at 1000, 60ms - 1000 = ~59.9ms <= 100ms (healthy).
    // Service 2 was beat at 40ms, 60ms - 40ms = 20ms <= 50ms (healthy).
    EXPECT_TRUE(monitor.checkHealth(60'000'000, failedId, lastBeat, limit));

    // Time at 120ms without any beats: Service 1 timeout (120ms - 1000 > 100ms)
    EXPECT_FALSE(monitor.checkHealth(120'000'000, failedId, lastBeat, limit));
    EXPECT_EQ(failedId, 1u);
}

TEST(SafetyTest, WatchdogSupervisorKickAndSuppression)
{
    using SafetyRuntime = RuntimeBuilder
        ::WithEvents<SafetyTestEvents>
        ::Build;

    class SafetyApp : public Application<SafetyApp, SafetyTestEvents> {
    public:
        int timeoutsDetected = 0;
        uint32_t timedOutServiceId = 0;

        void onRegisterHandlers() {
            this->on([this](const WatchdogTimeoutEvent& e) {
                timeoutsDetected++;
                timedOutServiceId = e.serviceId;
            });
        }
    };

    SafetyRuntime runtime;
    SafetyApp app;
    runtime.initialize(app);

    WatchdogSupervisor<4, ManualClockPolicy> supervisor;
    int hardwareKicks = 0;

    supervisor.setWatchdogKickCallback([](void* ctx) {
        (*static_cast<int*>(ctx))++;
    }, &hardwareKicks);

    // Register Worker 10 (timeout: 1000 us = 1,000,000 ns)
    ManualClockPolicy::reset();
    supervisor.registerService(10, 1'000'000);

    // Initial check at 0ns: should kick
    EXPECT_TRUE(supervisor.supervise(runtime.eventSink()));
    EXPECT_EQ(hardwareKicks, 1);
    EXPECT_EQ(supervisor.totalKicks(), 1u);
    EXPECT_EQ(supervisor.totalSuppressions(), 0u);

    // Advance clock by 500us (within budget) and beat
    ManualClockPolicy::advance(500'000);
    supervisor.beat(10);
    EXPECT_TRUE(supervisor.supervise(runtime.eventSink()));
    EXPECT_EQ(hardwareKicks, 2);

    // Advance clock by 2000us without beating -> timeout violation
    ManualClockPolicy::advance(2'000'000);
    EXPECT_FALSE(supervisor.supervise(runtime.eventSink()));
    EXPECT_EQ(hardwareKicks, 2); // Kick was suppressed!
    EXPECT_EQ(supervisor.totalSuppressions(), 1u);

    // Pump runtime to process posted WatchdogTimeoutEvent
    runtime.pump();
    EXPECT_EQ(app.timeoutsDetected, 1);
    EXPECT_EQ(app.timedOutServiceId, 10u);

    runtime.shutdown();
}

TEST(SafetyTest, CircuitBreakerStateTransitions)
{
    // Threshold = 2 failures, Recovery = 50ms
    CircuitBreaker<2, 50> breaker;
    EXPECT_EQ(breaker.state(), CircuitState::Closed);
    EXPECT_TRUE(breaker.allowExecution());

    // 1st failure: still Closed
    EXPECT_FALSE(breaker.execute([]() { return false; }));
    EXPECT_EQ(breaker.state(), CircuitState::Closed);
    EXPECT_EQ(breaker.failureCount(), 1u);

    // 2nd failure: trips Open!
    EXPECT_FALSE(breaker.execute([]() { return false; }));
    EXPECT_EQ(breaker.state(), CircuitState::Open);
    EXPECT_FALSE(breaker.allowExecution());

    // While Open, executions are immediately rejected without calling the function
    bool executed = false;
    EXPECT_FALSE(breaker.execute([&]() {
        executed = true;
        return true;
    }));
    EXPECT_FALSE(executed);

    // Wait for recovery timeout (50ms cooldown)
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    // Now state should transition to HalfOpen and allow canary execution
    EXPECT_EQ(breaker.state(), CircuitState::HalfOpen);
    EXPECT_TRUE(breaker.allowExecution());

    // Canary execution succeeds -> restores Closed state
    EXPECT_TRUE(breaker.execute([]() { return true; }));
    EXPECT_EQ(breaker.state(), CircuitState::Closed);
    EXPECT_EQ(breaker.failureCount(), 0u);
}

TEST(SafetyTest, WatchdogServiceAutonomousSupervision) {
    using SafetyRuntime = corium::RuntimeBuilder
        ::WithEvents<WdServiceEvents>
        ::WithPriorityQueue<16, 64>
        ::Build;

    SafetyRuntime runtime;
    MonitoredApp app;

    // Register service 42 with 25ms timeout
    app.wdService.registerService(42, 25'000'000);

    runtime.initialize(app);

    // Beat for the first 30ms (3 times)
    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        app.wdService.beat(42);
        runtime.pump();
    }

    // Now stop beating and wait for timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(45));
    runtime.pump();

    EXPECT_TRUE(app.timeoutTriggered);
    EXPECT_EQ(app.timedOutServiceId, 42u);

    runtime.shutdown();
}

TEST(SafetyTest, PanicHandlerHook) {
    static bool panicCalled = false;
    static int panicLine = 0;

    corium::setPanicHandler([](const char* /*file*/, int line, const char* /*msg*/) noexcept {
        panicCalled = true;
        panicLine = line;
    });

    CORIUM_PANIC("Simulated bare-metal fault");
    EXPECT_TRUE(panicCalled);
    EXPECT_GT(panicLine, 0);

    // Reset handler
    corium::setPanicHandler(nullptr);
}
