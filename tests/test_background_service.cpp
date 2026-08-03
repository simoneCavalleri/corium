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

class TestConsumerService : public BackgroundService<> {
public:
    TestConsumerService() {
        on([this](const SignalEvent& e) {
            _signalsReceived++;
            _lastSignalId = e.id;
            // Echo back to main event bus
            postEvent(TickEvent{0.1 * e.id});
        });
    }

    void run(std::stop_token stopToken) {
        while (!stopToken.stop_requested()) {
            waitAndPump(stopToken, std::chrono::milliseconds(20));
        }
    }

    int signalsReceived() const { return _signalsReceived; }
    uint32_t lastSignalId() const { return _lastSignalId; }

private:
    int _signalsReceived = 0;
    uint32_t _lastSignalId = 0;
};

class InterServiceProducer : public BackgroundService<> {
public:
    void run(std::stop_token stopToken) {
        uint32_t id = 1;
        while (!stopToken.stop_requested()) {
            sendToService<TestConsumerService>(SignalEvent{id++});
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    }
};

class ServiceCommunicationApp : public AppCoreT<ServiceCommunicationApp, Runtime::EventBusType> {
public:
    TestConsumerService consumerService;
    InterServiceProducer producerService;
    int tickEchoesReceived = 0;

    void onConfigureServices(ServiceRegistry& registry) {
        registry.registerService(consumerService);
        registry.registerService(producerService);
    }

    void onRegisterHandlers() {
        on([this](const TickEvent&) {
            tickEchoesReceived++;
            if (tickEchoesReceived >= 5) {
                requestQuit();
            }
        });
    }
};

TEST(BackgroundServiceTest, ConsumerServiceAndServiceToServiceCommunication) {
    Runtime runtime;
    ServiceCommunicationApp app;

    runtime.initialize(app);

    while (!runtime.quitRequested()) {
        runtime.waitAndPump(std::chrono::milliseconds(50));
    }

    EXPECT_GE(app.tickEchoesReceived, 5);
    EXPECT_GE(app.consumerService.signalsReceived(), 5);
    EXPECT_GE(app.consumerService.lastSignalId(), 5u);

    runtime.shutdown();
}

TEST(BackgroundServiceTest, ServiceRegistryTypedLookup) {
    ServiceRegistry registry;
    TestConsumerService consumer;
    InterServiceProducer producer;

    EXPECT_TRUE(registry.registerService(consumer));
    EXPECT_TRUE(registry.registerService(producer));

    EXPECT_EQ(registry.getService<TestConsumerService>(), &consumer);
    EXPECT_EQ(registry.getService<InterServiceProducer>(), &producer);
    EXPECT_TRUE(static_cast<bool>(registry.getServiceSink<TestConsumerService>()));
}

class PassiveThreadlessService : public Service<> {
public:
    PassiveThreadlessService() {
        on([this](const SignalEvent& e) {
            _eventsHandled++;
            _lastVal = e.id;
        });
    }

    int eventsHandled() const { return _eventsHandled; }
    uint32_t lastVal() const { return _lastVal; }

private:
    int _eventsHandled = 0;
    uint32_t _lastVal = 0;
};

TEST(ServiceTest, PassiveThreadlessServicePump) {
    PassiveThreadlessService service;
    service.sink().post(SignalEvent{42});
    service.sink().post(SignalEvent{100});

    EXPECT_EQ(service.eventsHandled(), 0);
    EXPECT_EQ(service.pump(), 2u);
    EXPECT_EQ(service.eventsHandled(), 2);
    EXPECT_EQ(service.lastVal(), 100u);
}


