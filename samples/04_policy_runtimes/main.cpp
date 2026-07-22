// =============================================================================
// Corium Sample 04 — Policy-Based Runtimes & Signaling Strategies (Bare Metal & Host)
//
// This example demonstrates configuring Corium Runtimes for different target environments:
//   1. Real-Time / Bare Metal: Busy-spin polling (NoSignalPolicy) for sub-microsecond latency.
//   2. C++20 Futex / Host: Zero-mutex signaling via std::atomic::wait() (AtomicWaitSignalPolicy).
// =============================================================================

#include <corium/corium.hpp>
#include <chrono>
#include <iostream>

using namespace corium;

template <typename EventBusType>
class PolicyDemoApp : public AppCoreT<PolicyDemoApp<EventBusType>, EventBusType> {
public:
    void onRegisterHandlers() {
        this->on([this](const TickEvent& e) {
            _count++;
            std::cout << "   Received Tick #" << _count << " (dt: " << e.deltaTime << "s)\n";
            if (_count >= 3) {
                this->requestQuit();
            }
        });
    }

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
    PolicyDemoApp<RealtimeRuntime::EventBusType> app;

    runtime.initialize(app);
    runtime.eventSink().post(TickEvent{0.001});
    runtime.eventSink().post(TickEvent{0.002});
    runtime.eventSink().post(TickEvent{0.003});

    while (!runtime.quitRequested()) {
        runtime.pump();
    }

    runtime.shutdown();
    std::cout << "   Real-Time Busy-Spin Runtime finished.\n";
}

#ifndef CORIUM_BARE_METAL
void runFutexAtomicWaitDemo() {
    std::cout << "\n--- 2. C++20 Futex Host Runtime (AtomicWaitSignalPolicy) ---\n";

    using FutexRuntime = RuntimeBuilder<>
        ::WithSignalPolicy<AtomicWaitSignalPolicy>
        ::Build;

    FutexRuntime runtime;
    PolicyDemoApp<FutexRuntime::EventBusType> app;

    runtime.initialize(app);
    runtime.eventSink().post(TickEvent{0.01});
    runtime.eventSink().post(TickEvent{0.02});
    runtime.eventSink().post(TickEvent{0.03});

    while (!runtime.quitRequested()) {
        runtime.waitAndPump(std::chrono::milliseconds(50));
    }

    runtime.shutdown();
    std::cout << "   Futex Runtime finished.\n";
}
#endif

int main() {
    std::cout << "========================================\n";
    std::cout << "Corium Sample 04: Policy-Based Runtimes\n";
    std::cout << "========================================\n";

    runRealtimeBusySpinDemo();
#ifndef CORIUM_BARE_METAL
    runFutexAtomicWaitDemo();
#endif

    std::cout << "\nAll policy runtime demonstrations completed successfully!\n";
    return 0;
}
