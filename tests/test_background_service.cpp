#include <gtest/gtest.h>
#include <corium/corium.hpp>

using namespace corium;

class TestProducerService : public BackgroundService {
public:
    using BackgroundService::BackgroundService;

    void run(std::stop_token stopToken) override {
        while (!stopToken.stop_requested()) {
            postEvent(TickEvent{0.05});
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
};

class ServiceTestApp : public AppCore {
public:
    int tickEventsReceived = 0;

protected:
    void onConfigureServices(ServiceRegistry& registry) override {
        registry.addService<TestProducerService>();
    }

    void onRegisterHandlers() override {
        on([this](const TickEvent&) {
            tickEventsReceived++;
            if (tickEventsReceived >= 5) {
                requestQuit();
            }
        });
    }

    void onInitialize() override {}
};

TEST(BackgroundServiceTest, ServiceThreadLifecycleAndEventPosting) {
    Runtime runtime;
    ServiceTestApp app;

    runtime.initialize(app);

    while (!runtime.quitRequested()) {
        runtime.waitAndPump(std::chrono::milliseconds(50));
    }

    EXPECT_GE(app.tickEventsReceived, 5);

    runtime.shutdown();
}
