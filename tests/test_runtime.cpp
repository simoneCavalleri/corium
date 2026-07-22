#include <gtest/gtest.h>
#include <corium/corium.hpp>

using namespace corium;

template <typename EventBusType = Runtime::EventBusType>
class TestApp : public AppCoreT<TestApp<EventBusType>, EventBusType> {
public:
    int tickCount = 0;
    bool initialized = false;
    bool shutdownCalled = false;

    void onRegisterHandlers() {
        this->on([this](const TickEvent&) {
            tickCount++;
            if (tickCount >= 3) {
                this->requestQuit();
            }
        });
    }

    void onInitialize() {
        initialized = true;
    }

    void onShutdown() {
        shutdownCalled = true;
    }
};

TEST(RuntimeTest, BasicLifecycleAndPump) {
    Runtime runtime;
    TestApp<> app;

    runtime.initialize(app);
    EXPECT_TRUE(app.initialized);
    EXPECT_FALSE(runtime.quitRequested());

    runtime.eventSink().post(TickEvent{0.01});
    runtime.eventSink().post(TickEvent{0.02});
    runtime.pump();

    EXPECT_EQ(app.tickCount, 2);
    EXPECT_FALSE(runtime.quitRequested());

    runtime.eventSink().post(TickEvent{0.03});
    runtime.pump();

    EXPECT_EQ(app.tickCount, 3);
    EXPECT_TRUE(runtime.quitRequested());

    runtime.shutdown();
    EXPECT_TRUE(app.shutdownCalled);
}

TEST(RuntimeTest, PolicyRuntimes) {
    // 1. NoSignalPolicy Busy-Spin Runtime
    using BusySpinRuntime = RuntimeBuilder<>
        ::WithSignalPolicy<NoSignalPolicy>
        ::Build;

    BusySpinRuntime spinRuntime;
    TestApp<BusySpinRuntime::EventBusType> spinApp;

    spinRuntime.initialize(spinApp);
    spinRuntime.eventSink().post(TickEvent{0.1});
    spinRuntime.eventSink().post(TickEvent{0.2});
    spinRuntime.eventSink().post(TickEvent{0.3});

    spinRuntime.pump();
    EXPECT_TRUE(spinRuntime.quitRequested());
    spinRuntime.shutdown();

    // 2. StoragePolicy Customization
    using LargeRuntime = RuntimeBuilder<>
        ::WithStoragePolicy<LargeStoragePolicy>
        ::Build;

    LargeRuntime largeRuntime;
    TestApp<LargeRuntime::EventBusType> largeApp;

    largeRuntime.initialize(largeApp);
    largeRuntime.eventSink().post(TickEvent{0.1});
    largeRuntime.eventSink().post(TickEvent{0.2});
    largeRuntime.eventSink().post(TickEvent{0.3});

    largeRuntime.pump();
    EXPECT_TRUE(largeRuntime.quitRequested());
    largeRuntime.shutdown();
}

TEST(RuntimeTest, StateMachineTransitions) {
    Runtime runtime;
    EXPECT_EQ(runtime.state(), Runtime::State::Created);

    TestApp<> app;
    runtime.initialize(app);
    EXPECT_EQ(runtime.state(), Runtime::State::Running);

    runtime.shutdown();
    EXPECT_EQ(runtime.state(), Runtime::State::Terminated);
}

TEST(RuntimeTest, BuilderOrderIndependence) {
    using GameEvents = std::variant<QuitEvent, TickEvent>;

    using OrderA = RuntimeBuilder<>
        ::WithCapacity<4096>
        ::WithEvents<GameEvents>
        ::WithStoragePolicy<LargeStoragePolicy>
        ::Build;

    using OrderB = RuntimeBuilder<>
        ::WithStoragePolicy<LargeStoragePolicy>
        ::WithEvents<GameEvents>
        ::WithCapacity<4096>
        ::Build;

    static_assert(std::is_same_v<OrderA, OrderB>, "RuntimeBuilder must be order-independent!");
    EXPECT_TRUE((std::is_same_v<OrderA, OrderB>));
}
