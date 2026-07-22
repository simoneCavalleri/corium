#include <gtest/gtest.h>
#include <corium/corium.hpp>
#include <chrono>
#include <iostream>
#include <thread>

using namespace corium;

class TestProducerService : public BackgroundService<> {
public:
    void run(std::stop_token stopToken) {
        while (!stopToken.stop_requested()) {
            _tickCount++;
            postEvent(TickEvent{0.05});
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    int count() const { return _tickCount; }

private:
    int _tickCount = 0;
};

class ServiceTestApp : public AppCoreT<ServiceTestApp, Runtime::EventBusType> {
public:
    TestProducerService producerService;
    int tickEventsReceived = 0;

    void onConfigureServices(ServiceRegistry& registry) {
        registry.registerService(producerService);
    }

    void onRegisterHandlers() {
        on([this](const TickEvent&) {
            tickEventsReceived++;
            if (tickEventsReceived >= 5) {
                requestQuit();
            }
        });
    }
};

TEST(BackgroundServiceTest, ThreadServiceLifecycleAndEventPosting) {
    Runtime runtime;
    ServiceTestApp app;

    runtime.initialize(app);

    while (!runtime.quitRequested()) {
        runtime.waitAndPump(std::chrono::milliseconds(50));
    }

    EXPECT_GE(app.tickEventsReceived, 5);
    EXPECT_GE(app.producerService.count(), 5);

    runtime.shutdown();
}
