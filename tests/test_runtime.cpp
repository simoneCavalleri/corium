#include <gtest/gtest.h>
#include <corium/corium.hpp>

using namespace corium;

class TestApp : public AppCore {
public:
    int tickCount = 0;
    bool initialized = false;
    bool shutdownCalled = false;

protected:
    void onRegisterHandlers() override {
        on([this](const TickEvent&) {
            tickCount++;
            if (tickCount >= 3) {
                requestQuit();
            }
        });
    }

    void onInitialize() override {
        initialized = true;
    }

    void onShutdown() override {
        shutdownCalled = true;
    }
};

TEST(RuntimeTest, BasicLifecycleAndPump) {
    Runtime runtime;
    TestApp app;

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

TEST(RuntimeTest, WaitAndPumpTimeout) {
    Runtime runtime;
    TestApp app;

    runtime.initialize(app);

    // Wait and pump when empty should timeout safely without processing events
    auto processed = runtime.waitAndPump(std::chrono::milliseconds(10));
    EXPECT_EQ(processed, 0);

    runtime.eventSink().post(TickEvent{0.1});
    processed = runtime.waitAndPump(std::chrono::milliseconds(10));
    EXPECT_EQ(processed, 1);

    runtime.shutdown();
}

TEST(RuntimeTest, PolicyRuntimes) {
    // 1. AtomicWaitSignalPolicy Futex Runtime
    using FutexRuntime = RuntimeBuilder<>
        ::WithSignalPolicy<AtomicWaitSignalPolicy>
        ::Build;

    FutexRuntime futexRuntime;
    TestApp futexApp;

    futexRuntime.initialize(futexApp);
    futexRuntime.eventSink().post(TickEvent{0.1});
    futexRuntime.eventSink().post(TickEvent{0.2});
    futexRuntime.eventSink().post(TickEvent{0.3});

    futexRuntime.waitAndPump(std::chrono::milliseconds(50));
    EXPECT_TRUE(futexRuntime.quitRequested());
    futexRuntime.shutdown();

    // 2. NoSignalPolicy Busy-Spin Runtime
    using BusySpinRuntime = RuntimeBuilder<>
        ::WithSignalPolicy<NoSignalPolicy>
        ::Build;

    BusySpinRuntime spinRuntime;
    TestApp spinApp;

    spinRuntime.initialize(spinApp);
    spinRuntime.eventSink().post(TickEvent{0.1});
    spinRuntime.eventSink().post(TickEvent{0.2});
    spinRuntime.eventSink().post(TickEvent{0.3});

    spinRuntime.pump();
    EXPECT_TRUE(spinRuntime.quitRequested());
    spinRuntime.shutdown();
}
