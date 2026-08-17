#include <gtest/gtest.h>
#include <corium/corium.hpp>
#include <chrono>
#include <thread>

using namespace corium;

// 1. Producer Service using ProducerBackgroundService (No incoming queue/reactor footprint)
class DummyProducerService : public ProducerBackgroundService<> {
public:
    void run(std::stop_token stopToken) {
        while (!stopToken.stop_requested()) {
            _tickCount++;
            post(TickEvent{0.01});
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    int tickCount() const { return _tickCount; }

private:
    int _tickCount = 0;
};

// Application using DummyProducerService
class ProducerTestApp : public Application<ProducerTestApp, Runtime::EventBusType> {
public:
    DummyProducerService producerService;
    int receivedTicks = 0;

    void onConfigureServices(ServiceRegistry& registry) {
        registry.registerService(producerService);
    }

    void onRegisterHandlers() {
        on([this](const TickEvent&) {
            receivedTicks++;
            if (receivedTicks >= 3) {
                requestQuit();
            }
        });
    }
};

TEST(NoQueueServiceTest, SizeComparison) {
    // ProducerService should be drastically smaller than full default Service
    EXPECT_LT(sizeof(ProducerService<>), sizeof(Service<>));
    EXPECT_LT(sizeof(ProducerBackgroundService<>), sizeof(BackgroundService<>));
}

TEST(NoQueueServiceTest, ProducerBackgroundServiceLifecycle) {
    Runtime runtime;
    ProducerTestApp app;

    runtime.initialize(app);

    while (!runtime.quitRequested()) {
        runtime.waitAndPump(std::chrono::milliseconds(50));
    }

    EXPECT_GE(app.receivedTicks, 3);
    EXPECT_GE(app.producerService.tickCount(), 3);

    runtime.shutdown();
}

TEST(NoQueueServiceTest, SendToProducerServiceFailsSafely) {
    ServiceRegistry registry;
    DummyProducerService producer;
    registry.registerService(producer);

    ServiceContext ctx;
    registry.initialize(ctx);

    // Attempting to post directly to a ProducerService (no queue) should return false safely
    bool result = ctx.sendToService<DummyProducerService>(SignalEvent{42});
    EXPECT_FALSE(result);

    registry.shutdown();
}
