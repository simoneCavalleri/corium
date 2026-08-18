#include <gtest/gtest.h>
#include <corium/corium.hpp>
#include <vector>

using namespace corium;

struct BatchNumberEvent {
    int value;
};

using BatchEvents = std::variant<QuitEvent, BatchNumberEvent>;

TEST(BatchPumpTest, EventBusProcessBatchAndDrain)
{
    BasicEventBus<BatchEvents> bus;
    std::vector<int> received;

    bus.registerHandler<BatchNumberEvent>([&received](const BatchNumberEvent& e) {
        received.push_back(e.value);
    });
    bus.seal();

    for (int i = 0; i < 50; ++i) {
        bus.post(BatchNumberEvent{i});
    }

    // Process batch of 10
    EXPECT_EQ(bus.processBatch(10), 10u);
    EXPECT_EQ(received.size(), 10u);
    EXPECT_EQ(received.back(), 9);

    // Process batch of 15
    EXPECT_EQ(bus.processBatch(15), 15u);
    EXPECT_EQ(received.size(), 25u);
    EXPECT_EQ(received.back(), 24);

    // Drain remaining 25
    EXPECT_EQ(bus.drain(), 25u);
    EXPECT_EQ(received.size(), 50u);
    EXPECT_EQ(received.back(), 49);

    // Drain when empty
    EXPECT_EQ(bus.drain(), 0u);
}

TEST(BatchPumpTest, RuntimePumpBatchAndDrain)
{
    using BatchRuntime = RuntimeBuilder::WithEvents<BatchEvents>::Build;

    class App : public Application<App, BatchEvents> {
    public:
        std::vector<int> values;
        void onRegisterHandlers() {
            this->on([this](const BatchNumberEvent& e) {
                values.push_back(e.value);
            });
        }
    };

    App app;
    BatchRuntime runtime;
    runtime.initialize(app);

    for (int i = 0; i < 35; ++i) {
        runtime.eventSink().post(BatchNumberEvent{i});
    }

    // Pump with batch size 10, max 20 total
    EXPECT_EQ(runtime.pumpBatch(10, 20), 20u);
    EXPECT_EQ(app.values.size(), 20u);

    // Drain remaining 15
    EXPECT_EQ(runtime.drain(), 15u);
    EXPECT_EQ(app.values.size(), 35u);

    runtime.shutdown();
}
