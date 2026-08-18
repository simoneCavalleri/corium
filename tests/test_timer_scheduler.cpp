#include <gtest/gtest.h>
#include <corium/corium.hpp>
#include <vector>
#include <thread>
#include <chrono>

using namespace corium;

struct TimerTickEvent {
    int count;
};

struct ScheduledAlertEvent {
    const char* message;
};

using TimerTestEvents = std::variant<QuitEvent, TimerTickEvent, ScheduledAlertEvent>;

TEST(TimerSchedulerTest, DelayedEventScheduling)
{
    TimerScheduler<TimerTestEvents, 16> scheduler;
    class MockSink {
    public:
        std::vector<TimerTestEvents> postedEvents;
        void post(TimerTestEvents evt, EventPriority = EventPriority::Normal) {
            postedEvents.push_back(evt);
        }
    } sink;

    auto id = scheduler.scheduleDelayed(ScheduledAlertEvent{"Alarm!"}, std::chrono::milliseconds(50));
    EXPECT_NE(id, INVALID_TIMER_ID);
    EXPECT_EQ(scheduler.activeCount(), 1u);

    // Before delay expires
    auto postedCount = scheduler.processDueTimers(sink, std::chrono::steady_clock::now());
    EXPECT_EQ(postedCount, 0u);
    EXPECT_TRUE(sink.postedEvents.empty());

    // Wait until delay expires
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    postedCount = scheduler.processDueTimers(sink, std::chrono::steady_clock::now());
    EXPECT_EQ(postedCount, 1u);
    ASSERT_EQ(sink.postedEvents.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<ScheduledAlertEvent>(sink.postedEvents[0]));
    EXPECT_STREQ(std::get<ScheduledAlertEvent>(sink.postedEvents[0]).message, "Alarm!");

    // Single-shot timer should be inactive after execution
    EXPECT_EQ(scheduler.activeCount(), 0u);
}

TEST(TimerSchedulerTest, PeriodicEventScheduling)
{
    TimerScheduler<TimerTestEvents, 16> scheduler;
    class MockSink {
    public:
        int tickCount = 0;
        void post(TimerTestEvents evt, EventPriority = EventPriority::Normal) {
            if (std::holds_alternative<TimerTickEvent>(evt)) {
                tickCount++;
            }
        }
    } sink;

    auto id = scheduler.schedulePeriodic(TimerTickEvent{1}, std::chrono::milliseconds(20));
    EXPECT_NE(id, INVALID_TIMER_ID);
    EXPECT_EQ(scheduler.activeCount(), 1u);

    // Run 3 periodic cycles
    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        scheduler.processDueTimers(sink, std::chrono::steady_clock::now());
    }

    EXPECT_EQ(sink.tickCount, 3);
    EXPECT_EQ(scheduler.activeCount(), 1u); // Periodic timer remains active

    EXPECT_TRUE(scheduler.cancelTimer(id));
    EXPECT_EQ(scheduler.activeCount(), 0u);
}

TEST(TimerSchedulerTest, TimerCancellation)
{
    TimerScheduler<TimerTestEvents, 16> scheduler;
    class MockSink {
    public:
        int count = 0;
        void post(TimerTestEvents, EventPriority = EventPriority::Normal) {
            count++;
        }
    } sink;

    auto id = scheduler.scheduleDelayed(ScheduledAlertEvent{"Cancelled"}, std::chrono::milliseconds(20));
    EXPECT_EQ(scheduler.activeCount(), 1u);

    EXPECT_TRUE(scheduler.cancelTimer(id));
    EXPECT_EQ(scheduler.activeCount(), 0u);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    scheduler.processDueTimers(sink, std::chrono::steady_clock::now());

    EXPECT_EQ(sink.count, 0);
}

TEST(TimerSchedulerTest, RuntimeIntegrationWithApplication)
{
    using TimerRuntime = RuntimeBuilder
        ::WithEvents<TimerTestEvents>
        ::WithMaxTimers<16>
        ::Build;

    class App : public Application<App, TimerTestEvents> {
    public:
        int alertCount = 0;
        int tickCount = 0;

        void onRegisterHandlers() {
            this->on([this](const ScheduledAlertEvent&) {
                alertCount++;
            });

            this->on([this](const TimerTickEvent&) {
                tickCount++;
            });
        }
    };

    TimerRuntime runtime;
    App app;
    runtime.initialize(app);

    // Schedule delayed and periodic events via runtime and app
    runtime.scheduleDelayed(ScheduledAlertEvent{"Delayed Alert"}, std::chrono::milliseconds(30));
    runtime.schedulePeriodic(TimerTickEvent{1}, std::chrono::milliseconds(20));

    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        runtime.pump();
    }

    EXPECT_GE(app.alertCount, 1);
    EXPECT_GE(app.tickCount, 3);

    runtime.shutdown();
}

TEST(TimerSchedulerTest, PeriodicTimerNoCumulativeDrift)
{
    ManualClockPolicy::reset();
    TimerScheduler<TimerTestEvents, 16, ManualClockPolicy> scheduler;

    class MockSink {
    public:
        int tickCount = 0;
        void post(TimerTestEvents, EventPriority = EventPriority::Normal) {
            tickCount++;
        }
    } sink;

    // Schedule periodic timer every 100 microseconds (due at 100, 200, 300, 400...)
    auto id = scheduler.schedulePeriodic(TimerTickEvent{1}, std::chrono::microseconds(100));
    EXPECT_NE(id, INVALID_TIMER_ID);

    // 1. Tick 1 arrives with +5us jitter (at t = 105us)
    ManualClockPolicy::set(105);
    EXPECT_EQ(scheduler.processDueTimers(sink), 1u);
    EXPECT_EQ(sink.tickCount, 1);

    // 2. Advance to t = 199us: should NOT fire yet (target is t = 200us, NOT 205us)
    ManualClockPolicy::set(199);
    EXPECT_EQ(scheduler.processDueTimers(sink), 0u);
    EXPECT_EQ(sink.tickCount, 1);

    // 3. Advance to t = 202us (+2us jitter): fires tick 2 (target becomes t = 300us)
    ManualClockPolicy::set(202);
    EXPECT_EQ(scheduler.processDueTimers(sink), 1u);
    EXPECT_EQ(sink.tickCount, 2);

    // 4. Advance to t = 299us: should NOT fire yet
    ManualClockPolicy::set(299);
    EXPECT_EQ(scheduler.processDueTimers(sink), 0u);
    EXPECT_EQ(sink.tickCount, 2);

    // 5. Advance to t = 301us: fires tick 3 (target becomes t = 400us)
    ManualClockPolicy::set(301);
    EXPECT_EQ(scheduler.processDueTimers(sink), 1u);
    EXPECT_EQ(sink.tickCount, 3);

    scheduler.cancelTimer(id);
}

