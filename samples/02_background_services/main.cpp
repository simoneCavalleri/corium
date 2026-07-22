// =============================================================================
// Corium Sample 02 — Multi-Threaded Background Services (Zero-Heap MPSC)
//
// This example demonstrates:
//   1. Registering BackgroundServices with dedicated std::jthread worker loops.
//   2. Multiple asynchronous thread producers posting to the Lock-Free MPSC queue.
//   3. Registering background services inside onConfigureServices(ServiceRegistry& registry).
//   4. Automatic thread launching, stop_token signaling, and graceful join on shutdown.
// =============================================================================

#include <corium/corium.hpp>
#include <chrono>
#include <iostream>
#include <thread>

using namespace corium;

// 1. Background Sensor Producer Service (running on its own std::jthread)
class SensorService : public BackgroundService<> {
public:
    void run(std::stop_token stopToken) {
        double elapsed = 0.0;
        while (!stopToken.stop_requested()) {
            postEvent(TickEvent{elapsed});
            elapsed += 0.2;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
};

// 2. Background Signal Producer Service (running on its own std::jthread)
class SignalProducerService : public BackgroundService<> {
public:
    void run(std::stop_token stopToken) {
        uint32_t signalId = 0;
        while (!stopToken.stop_requested()) {
            signalId++;
            postEvent(SignalEvent{signalId});
            std::this_thread::sleep_for(std::chrono::milliseconds(350));
        }
    }
};

// 3. Application Managing Both Thread-Based Services via onConfigureServices
class ServiceApp : public AppCoreT<ServiceApp, Runtime::EventBusType> {
public:
    SensorService sensorService;
    SignalProducerService signalProducerService;

    void onConfigureServices(ServiceRegistry& registry) {
        registry.registerService(sensorService);
        registry.registerService(signalProducerService);
    }

    void onRegisterHandlers() {
        on([this](const TickEvent& e) {
            _tickCount++;
            std::cout << "[ServiceApp] Sensor Tick #" << _tickCount << " (time: " << e.deltaTime << "s)\n";
        });

        on([this](const SignalEvent& e) {
            std::cout << "[ServiceApp] SignalEvent received with ID: " << e.id << "\n";
            if (_tickCount >= 5) {
                std::cout << "[ServiceApp] Processed 5 ticks, requesting quit...\n";
                requestQuit();
            }
        });
    }

    void onInitialize() {
        std::cout << "[ServiceApp] All background service jthreads started.\n";
    }

private:
    int _tickCount = 0;
};

int main() {
    std::cout << "========================================\n";
    std::cout << "Corium Sample 02: Multi-Threaded Services\n";
    std::cout << "========================================\n";

    Runtime runtime;
    ServiceApp app;

    // Runtime automatically initializes the app and launches all service jthreads
    runtime.initialize(app);

    // Main thread acts as Single Consumer waiting and pumping events from the lock-free MPSC queue
    while (!runtime.quitRequested()) {
        runtime.waitAndPump(std::chrono::milliseconds(50));
    }

    // Runtime gracefully requests stop and joins all background jthreads
    runtime.shutdown();
    std::cout << "[Main] Exit complete.\n";
    return 0;
}
