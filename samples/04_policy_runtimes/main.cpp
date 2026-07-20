// =============================================================================
// Corium Sample 04 — Policy-Based Runtimes & Signaling Strategies
//
// This example demonstrates configuring Corium Runtimes for different target environment:
//   1. Real-Time / Game Engine: Busy-spin polling (NoSignalPolicy) for sub-microsecond latency.
//   2. C++20 Futex: Zero-mutex signaling via std::atomic::wait() (AtomicWaitSignalPolicy).
//   3. Power-Saving Desktop: Traditional Blocking Queue (BlockingQueuePolicy).
// =============================================================================

#include <corium/corium.hpp>
#include <iostream>

using namespace corium;

// Application used across policy tests
class PolicyDemoApp : public AppCore {
protected:
    void onRegisterHandlers() override {
        events().registerHandler<TickEvent>([this](const TickEvent& e) {
            _count++;
            std::cout << "   Received Tick #" << _count << " (dt: " << e.deltaTime << "s)\n";
            if (_count >= 3) {
                requestQuit();
            }
        });
    }

    void onInitialize() override {}

private:
    int _count = 0;
};

void runRealtimeBusySpinDemo() {
    std::cout << "\n--- 1. Real-Time Busy-Spin Runtime (NoSignalPolicy, Capacity: 4096) ---\n";
    
    using RealtimeRuntime = RuntimeBuilder<>
        ::WithCapacity<4096>
        ::WithSignalPolicy<NoSignalPolicy>
        ::Build;

    RealtimeRuntime runtime;
    PolicyDemoApp app;

    runtime.initialize(app);
    runtime.eventSink().post(TickEvent{0.001});
    runtime.eventSink().post(TickEvent{0.002});
    runtime.eventSink().post(TickEvent{0.003});

    // In a game or real-time loop, pump() is called continuously every frame
    while (!runtime.quitRequested()) {
        runtime.pump();
    }

    runtime.shutdown();
    std::cout << "   Real-Time Busy-Spin Runtime finished.\n";
}

void runFutexAtomicWaitDemo() {
    std::cout << "\n--- 2. C++20 Futex Runtime (AtomicWaitSignalPolicy) ---\n";

    using FutexRuntime = RuntimeBuilder<>
        ::WithSignalPolicy<AtomicWaitSignalPolicy>
        ::Build;

    FutexRuntime runtime;
    PolicyDemoApp app;

    runtime.initialize(app);
    runtime.eventSink().post(TickEvent{0.01});
    runtime.eventSink().post(TickEvent{0.02});
    runtime.eventSink().post(TickEvent{0.03});

    while (!runtime.quitRequested()) {
        runtime.pump();
    }

    runtime.shutdown();
    std::cout << "   Futex Runtime finished.\n";
}

void runBlockingQueueDemo() {
    std::cout << "\n--- 3. Power-Saving Runtime (BlockingQueuePolicy) ---\n";

    using BlockingRuntime = RuntimeBuilder<>
        ::WithQueuePolicy<BlockingQueuePolicy<DefaultEvents>>
        ::Build;

    BlockingRuntime runtime;
    PolicyDemoApp app;

    runtime.initialize(app);
    runtime.eventSink().post(TickEvent{0.1});
    runtime.eventSink().post(TickEvent{0.2});
    runtime.eventSink().post(TickEvent{0.3});

    while (!runtime.quitRequested()) {
        runtime.pump();
    }

    runtime.shutdown();
    std::cout << "   Power-Saving Runtime finished.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Corium Sample 04: Policy-Based Runtimes\n";
    std::cout << "========================================\n";

    runRealtimeBusySpinDemo();
    runFutexAtomicWaitDemo();
    runBlockingQueueDemo();

    std::cout << "\nAll policy runtime demonstrations completed successfully!\n";
    return 0;
}
