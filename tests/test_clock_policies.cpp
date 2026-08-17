#include <gtest/gtest.h>
#include <corium/corium.hpp>

using namespace corium;

struct TickCounterProvider {
    static inline uint64_t simulatedUs = 1000;
    static uint64_t nowUs() { return simulatedUs; }
};

struct MsCounterProvider {
    static inline uint32_t simulatedMs = 50;
    static uint32_t nowMs() { return simulatedMs; }
};

struct ClockTestEvent {
    int id;
};

using ClockTestEvents = std::variant<QuitEvent, ClockTestEvent>;

TEST(ClockPoliciesTest, ManualClockPolicyProgression)
{
    ManualClockPolicy::reset();
    EXPECT_EQ(ManualClockPolicy::now(), 0u);

    ManualClockPolicy::advance(500);
    EXPECT_EQ(ManualClockPolicy::now(), 500u);

    ManualClockPolicy::set(12345);
    EXPECT_EQ(ManualClockPolicy::now(), 12345u);

    EXPECT_TRUE(ManualClockPolicy::isDue(100, 50));
    EXPECT_TRUE(ManualClockPolicy::isDue(100, 100));
    EXPECT_FALSE(ManualClockPolicy::isDue(100, 101));
}

TEST(ClockPoliciesTest, MicrosecondTickClockPolicy)
{
    TickCounterProvider::simulatedUs = 2500;
    using Clock = MicrosecondTickClockPolicy<TickCounterProvider>;

    EXPECT_EQ(Clock::now(), 2500u);
    auto next = Clock::add(Clock::now(), std::chrono::microseconds(500));
    EXPECT_EQ(next, 3000u);
    EXPECT_TRUE(Clock::isDue(3000, 3000));
    EXPECT_FALSE(Clock::isDue(2999, 3000));
}

TEST(ClockPoliciesTest, MillisecondTickClockPolicy)
{
    MsCounterProvider::simulatedMs = 120;
    using Clock = MillisecondTickClockPolicy<MsCounterProvider>;

    EXPECT_EQ(Clock::now(), 120u);
    auto next = Clock::add(Clock::now(), std::chrono::milliseconds(30));
    EXPECT_EQ(next, 150u);
    EXPECT_TRUE(Clock::isDue(150, 150));
    EXPECT_FALSE(Clock::isDue(149, 150));
}

TEST(ClockPoliciesTest, TimerSchedulerWithManualClock)
{
    ManualClockPolicy::reset();
    TimerScheduler<ClockTestEvents, 16, ManualClockPolicy> scheduler;

    class MockSink {
    public:
        std::vector<int> received;
        void post(ClockTestEvents evt, EventPriority = EventPriority::Normal)
        {
            if (std::holds_alternative<ClockTestEvent>(evt)) {
                received.push_back(std::get<ClockTestEvent>(evt).id);
            }
        }
    } sink;

    // Schedule delayed timer at +1000us
    TimerId t1 = scheduler.scheduleDelayed(ClockTestEvent{1}, std::chrono::microseconds(1000));
    EXPECT_NE(t1, INVALID_TIMER_ID);
    EXPECT_EQ(scheduler.activeCount(), 1u);

    // Schedule periodic timer every 500us
    TimerId t2 = scheduler.schedulePeriodic(ClockTestEvent{2}, std::chrono::microseconds(500));
    EXPECT_NE(t2, INVALID_TIMER_ID);
    EXPECT_EQ(scheduler.activeCount(), 2u);

    // Step clock to 400us -> nothing due
    ManualClockPolicy::advance(400);
    EXPECT_EQ(scheduler.processDueTimers(sink), 0u);
    EXPECT_TRUE(sink.received.empty());

    // Step clock to 600us -> periodic timer t2 fires
    ManualClockPolicy::advance(200); // now = 600us
    EXPECT_EQ(scheduler.processDueTimers(sink), 1u);
    ASSERT_EQ(sink.received.size(), 1u);
    EXPECT_EQ(sink.received[0], 2);

    // Step clock to 1100us -> both t1 (1000) and t2 (1100) fire
    ManualClockPolicy::advance(500); // now = 1100us
    EXPECT_EQ(scheduler.processDueTimers(sink), 2u);
    ASSERT_EQ(sink.received.size(), 3u);
    EXPECT_EQ(sink.received[1], 1); // Delayed timer
    EXPECT_EQ(sink.received[2], 2); // Periodic timer

    // t1 was single-shot, so active count is now 1 (only t2 remaining)
    EXPECT_EQ(scheduler.activeCount(), 1u);

    // Cancel periodic timer
    EXPECT_TRUE(scheduler.cancelTimer(t2));
    EXPECT_EQ(scheduler.activeCount(), 0u);
}

TEST(ClockPoliciesTest, RuntimeWithManualClockBuilder)
{
    ManualClockPolicy::reset();

    using ManualRuntime = RuntimeBuilder
        ::WithEvents<ClockTestEvents>
        ::WithClockPolicy<ManualClockPolicy>
        ::Build;

    class App : public Application<App, ClockTestEvents> {
    public:
        int receivedId = 0;
        void onRegisterHandlers() {
            this->on([this](const ClockTestEvent& e) {
                receivedId = e.id;
            });
        }
    };

    ManualRuntime runtime;
    App app;
    runtime.initialize(app);

    runtime.scheduleDelayed(ClockTestEvent{42}, std::chrono::microseconds(100));
    runtime.pump();
    EXPECT_EQ(app.receivedId, 0); // Not yet due

    ManualClockPolicy::advance(150);
    runtime.pump();
    EXPECT_EQ(app.receivedId, 42); // Now due and dispatched

    runtime.shutdown();
}
