#include <gtest/gtest.h>
#include <corium/corium.hpp>
#include <chrono>
#include <iostream>
#include <thread>

using namespace corium;

class TestProducerService : public BackgroundService<> {
public:
    void run(const std::stop_token& stopToken) {
        while (!stopToken.stop_requested()) {
            _tickCount++;
            post(TickEvent{0.05});
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    [[nodiscard]] int count() const { return _tickCount; }

private:
    int _tickCount = 0;
};

class ServiceTestApp : public Application<ServiceTestApp, Runtime::EventBusType> {
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
            post(TickEvent{0.1 * e.id});
        });
    }

    void run(const std::stop_token& stopToken) {
        while (!stopToken.stop_requested()) {
            waitAndPump(stopToken, std::chrono::milliseconds(20));
        }
    }

    [[nodiscard]] int signalsReceived() const { return _signalsReceived; }
    [[nodiscard]] uint32_t lastSignalId() const { return _lastSignalId; }

private:
    int _signalsReceived = 0;
    uint32_t _lastSignalId = 0;
};

class InterServiceProducer : public BackgroundService<> {
public:
    void run(const std::stop_token& stopToken) {
        uint32_t id = 1;
        while (!stopToken.stop_requested()) {
            sendToService<TestConsumerService>(SignalEvent{id++});
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    }
};

class ServiceCommunicationApp : public Application<ServiceCommunicationApp, Runtime::EventBusType> {
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

    [[nodiscard]] int eventsHandled() const { return _eventsHandled; }
    [[nodiscard]] uint32_t lastVal() const { return _lastVal; }

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

class FaultyBackgroundService : public ProducerBackgroundService<> {
public:
    bool onErrorCalled = false;
    std::string capturedError;

    void onError(const std::exception_ptr& ep) {
        onErrorCalled = true;
        try {
            if (ep) std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            capturedError = e.what();
        }
    }

    void run(const std::stop_token&) {
        throw std::runtime_error("Simulated worker failure");
    }
};

class FaultyApp : public corium::Application<FaultyApp, DefaultEvents, 4> {
public:
    FaultyBackgroundService service;
    bool errorHandled = false;
    uint32_t lastErrorCode = 0;

    template <typename Registry>
    void onConfigureServices(Registry& registry) {
        registry.registerService(service);
    }

    void onRegisterHandlers() {
        this->on([this](const ErrorEvent& e) {
            errorHandled = true;
            lastErrorCode = e.code;
            this->requestQuit();
        });
    }
    size_t serviceCount() { return services().size(); }
    FaultyBackgroundService* findService() { return getService<FaultyBackgroundService>(); }
};

TEST(BackgroundServiceTest, WorkerExceptionHandlingAndErrorEventDispatch) {
    Runtime runtime;
    FaultyApp app;

    runtime.initialize(app);

    // Test Application::services() and Application::getService()
    EXPECT_EQ(app.findService(), &app.service);
    EXPECT_EQ(app.serviceCount(), 1u);

    // Wait for the worker to fail and post ErrorEvent
    auto start = std::chrono::steady_clock::now();
    while (!runtime.quitRequested() && (std::chrono::steady_clock::now() - start) < std::chrono::milliseconds(500)) {
        runtime.waitAndPump(std::chrono::milliseconds(20));
    }

    EXPECT_TRUE(app.errorHandled);
    EXPECT_EQ(app.lastErrorCode, 1u);
    EXPECT_TRUE(app.service.onErrorCalled);
    EXPECT_EQ(app.service.capturedError, "Simulated worker failure");

    runtime.shutdown();
}



