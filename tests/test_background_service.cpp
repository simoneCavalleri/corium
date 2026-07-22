#include <gtest/gtest.h>
#include <corium/corium.hpp>

using namespace corium;

class TestProducerService : public BackgroundService<> {
public:
    void poll() {
        _tickCount++;
        postEvent(TickEvent{0.05});
    }

    int count() const { return _tickCount; }

private:
    int _tickCount = 0;
};

class ServiceTestApp : public AppCoreT<ServiceTestApp, Runtime::EventBusType> {
public:
    int tickEventsReceived = 0;

    void onRegisterHandlers() {
        on([this](const TickEvent&) {
            tickEventsReceived++;
            if (tickEventsReceived >= 5) {
                requestQuit();
            }
        });
    }
};

TEST(BackgroundServiceTest, StaticServiceLifecycleAndEventPosting) {
    Runtime runtime;
    ServiceTestApp app;
    ServiceManager<TestProducerService> manager;

    runtime.initialize(app);
    manager.initialize(ServiceContext{runtime.eventSink()});

    while (!runtime.quitRequested()) {
        manager.poll();
        runtime.pump();
    }

    EXPECT_GE(app.tickEventsReceived, 5);
    EXPECT_GE(manager.get<TestProducerService>().count(), 5);

    manager.shutdown();
    runtime.shutdown();
}
