#include <gtest/gtest.h>
#include <corium/corium.hpp>
#include <vector>
#include <thread>
#include <atomic>

using namespace corium;

struct NormalEvent {
    int id;
};

struct EmergencyIsrEvent {
    int code;
};

using TestEvents = std::variant<QuitEvent, NormalEvent, EmergencyIsrEvent>;

TEST(PriorityQueueTest, PriorityOrdering)
{
    PriorityMpscQueuePolicy<TestEvents, 128, 128> queue;

    // Push 3 normal events
    EXPECT_TRUE(queue.tryPush(NormalEvent{1}, EventPriority::Normal).pushed);
    EXPECT_TRUE(queue.tryPush(NormalEvent{2}, EventPriority::Normal).pushed);
    EXPECT_TRUE(queue.tryPush(NormalEvent{3}, EventPriority::Normal).pushed);

    // Push 1 emergency high-priority event
    EXPECT_TRUE(queue.tryPush(EmergencyIsrEvent{999}, EventPriority::High).pushed);

    // Pop first: must be the EmergencyIsrEvent despite being pushed last
    TestEvents popped;
    EXPECT_TRUE(queue.tryPop(popped));
    EXPECT_TRUE(std::holds_alternative<EmergencyIsrEvent>(popped));
    EXPECT_EQ(std::get<EmergencyIsrEvent>(popped).code, 999);

    // Subsequent pops must be the normal events in FIFO order
    EXPECT_TRUE(queue.tryPop(popped));
    EXPECT_TRUE(std::holds_alternative<NormalEvent>(popped));
    EXPECT_EQ(std::get<NormalEvent>(popped).id, 1);

    EXPECT_TRUE(queue.tryPop(popped));
    EXPECT_TRUE(std::holds_alternative<NormalEvent>(popped));
    EXPECT_EQ(std::get<NormalEvent>(popped).id, 2);

    EXPECT_TRUE(queue.tryPop(popped));
    EXPECT_TRUE(std::holds_alternative<NormalEvent>(popped));
    EXPECT_EQ(std::get<NormalEvent>(popped).id, 3);

    EXPECT_TRUE(queue.empty());
}

TEST(PriorityQueueTest, EventBusPriorityDispatch)
{
    using PriorityRuntime = RuntimeBuilder<>
        ::WithEvents<TestEvents>
        ::WithPriorityQueue<128, 128>
        ::Build;

    class PriorityTestApp : public AppCoreT<PriorityTestApp, PriorityRuntime::EventBusType> {
    public:
        std::vector<int> dispatchOrder;

        void onRegisterHandlers()
        {
            on([this](const NormalEvent& e) {
                dispatchOrder.push_back(e.id);
            });

            on([this](const EmergencyIsrEvent& e) {
                dispatchOrder.push_back(e.code);
            });
        }
    };

    PriorityRuntime runtime;
    PriorityTestApp app;
    runtime.initialize(app);

    auto sink = runtime.eventSink();

    // Post Normal events
    sink.post(NormalEvent{100});
    sink.post(NormalEvent{101});

    // Post High priority event (e.g. from interrupt)
    sink.postHighPriority(EmergencyIsrEvent{9999});

    // Post another Normal event
    sink.post(NormalEvent{102});

    runtime.pump();

    // Verification: EmergencyIsrEvent (9999) must be processed first!
    ASSERT_EQ(app.dispatchOrder.size(), 4u);
    EXPECT_EQ(app.dispatchOrder[0], 9999);
    EXPECT_EQ(app.dispatchOrder[1], 100);
    EXPECT_EQ(app.dispatchOrder[2], 101);
    EXPECT_EQ(app.dispatchOrder[3], 102);

    runtime.shutdown();
}

TEST(PriorityQueueTest, ConcurrentMultiProducerPush)
{
    using PriorityRuntime = RuntimeBuilder<>
        ::WithEvents<TestEvents>
        ::WithPriorityQueue<1024, 1024>
        ::Build;

    PriorityRuntime runtime;
    
    class MultiProducerApp : public AppCoreT<MultiProducerApp, PriorityRuntime::EventBusType> {
    public:
        std::atomic<int> normalCount{0};
        std::atomic<int> highCount{0};

        void onRegisterHandlers()
        {
            on([this](const NormalEvent&) {
                normalCount++;
            });

            on([this](const EmergencyIsrEvent&) {
                highCount++;
            });
        }
    };

    MultiProducerApp app;
    runtime.initialize(app);
    auto sink = runtime.eventSink();

    constexpr int NUM_NORMAL_THREADS = 4;
    constexpr int NUM_HIGH_THREADS = 2;
    constexpr int EVENTS_PER_THREAD = 200;

    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_NORMAL_THREADS; ++t) {
        threads.emplace_back([sink, t]() {
            for (int i = 0; i < EVENTS_PER_THREAD; ++i) {
                sink.post(NormalEvent{t * 1000 + i});
            }
        });
    }

    for (int t = 0; t < NUM_HIGH_THREADS; ++t) {
        threads.emplace_back([sink, t]() {
            for (int i = 0; i < EVENTS_PER_THREAD; ++i) {
                sink.postHighPriority(EmergencyIsrEvent{t * 5000 + i});
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    runtime.pump();

    EXPECT_EQ(app.normalCount.load(), NUM_NORMAL_THREADS * EVENTS_PER_THREAD);
    EXPECT_EQ(app.highCount.load(), NUM_HIGH_THREADS * EVENTS_PER_THREAD);

    runtime.shutdown();
}
