#include <gtest/gtest.h>
#include <corium/corium.hpp>
#include <vector>

using namespace corium;

struct TelemetryEvent {
    int sequenceNumber;
};

using OverflowTestEvents = std::variant<QuitEvent, TelemetryEvent>;

TEST(OverflowPolicyTest, DropNewestPolicy)
{
    // Bounded queue capacity = 4
    using DropNewestRuntime = RuntimeBuilder<>
        ::WithEvents<OverflowTestEvents>
        ::WithCapacity<4>
        ::WithOverflowPolicy<DropNewestOverflowPolicy>
        ::Build;

    DropNewestRuntime runtime;
    auto sink = runtime.eventSink();

    // Push 4 events (filling the queue)
    for (int i = 1; i <= 4; ++i) {
        sink.post(TelemetryEvent{i});
    }

    // Push 5th event (queue is full)
    sink.post(TelemetryEvent{5});

    std::vector<int> received;
    class App : public AppCoreT<App, DropNewestRuntime::EventBusType> {
    public:
        std::vector<int>* recPtr;
        void onRegisterHandlers() {
            on([this](const TelemetryEvent& e) {
                recPtr->push_back(e.sequenceNumber);
            });
        }
    } app;
    app.recPtr = &received;

    runtime.initialize(app);
    runtime.pump();

    // Verification: Events 1..4 received, event 5 dropped!
    ASSERT_EQ(received.size(), 4u);
    EXPECT_EQ(received[0], 1);
    EXPECT_EQ(received[1], 2);
    EXPECT_EQ(received[2], 3);
    EXPECT_EQ(received[3], 4);

    runtime.shutdown();
}

TEST(OverflowPolicyTest, AuditOverflowPolicyCounter)
{
    using AuditRuntime = RuntimeBuilder<>
        ::WithEvents<OverflowTestEvents>
        ::WithCapacity<8>
        ::WithOverflowPolicy<AuditOverflowPolicy>
        ::Build;

    AuditRuntime runtime;
    auto sink = runtime.eventSink();

    EXPECT_EQ(runtime.overflowPolicy().overflowCount(), 0u);

    // Push 8 events to fill capacity
    for (int i = 1; i <= 8; ++i) {
        sink.post(TelemetryEvent{i});
    }

    // Push 5 additional events while full
    for (int i = 9; i <= 13; ++i) {
        sink.post(TelemetryEvent{i});
    }

    EXPECT_EQ(runtime.overflowPolicy().overflowCount(), 5u);

    runtime.overflowPolicy().resetOverflowCount();
    EXPECT_EQ(runtime.overflowPolicy().overflowCount(), 0u);
}

TEST(OverflowPolicyTest, DropOldestPolicyEviction)
{
    using DropOldestRuntime = RuntimeBuilder<>
        ::WithEvents<OverflowTestEvents>
        ::WithCapacity<4>
        ::WithOverflowPolicy<DropOldestOverflowPolicy>
        ::Build;

    DropOldestRuntime runtime;
    auto sink = runtime.eventSink();

    // Push events 1 to 6 into capacity=4 queue
    for (int i = 1; i <= 6; ++i) {
        sink.post(TelemetryEvent{i});
    }

    std::vector<int> received;
    class App : public AppCoreT<App, DropOldestRuntime::EventBusType> {
    public:
        std::vector<int>* recPtr;
        void onRegisterHandlers() {
            on([this](const TelemetryEvent& e) {
                recPtr->push_back(e.sequenceNumber);
            });
        }
    } app;
    app.recPtr = &received;

    runtime.initialize(app);
    runtime.pump();

    // Verification: Oldest events (1, 2) evicted; newest events (3, 4, 5, 6) preserved!
    ASSERT_EQ(received.size(), 4u);
    EXPECT_EQ(received[0], 3);
    EXPECT_EQ(received[1], 4);
    EXPECT_EQ(received[2], 5);
    EXPECT_EQ(received[3], 6);

    runtime.shutdown();
}
