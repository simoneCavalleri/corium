// =============================================================================
// Corium Sample 02 — Multi-Producer Background Services (Zero-Heap)
//
// This example demonstrates:
//   1. Static non-allocating BackgroundService instances.
//   2. Multiple producers pushing to the lock-free MPSC event queue.
//   3. Auto-deduced lambda event handlers via on(...).
//   4. Clean main event loop with zero dynamic allocations.
// =============================================================================

#include <corium/corium.hpp>
#include <iostream>

using namespace corium;

// 1. Background Sensor Service
class SensorService : public BackgroundService<> {
public:
    void poll() {
        _tickCounter++;
        if (_tickCounter % 2 == 0) {
            postEvent(TickEvent{_elapsed});
            _elapsed += 0.2;
        }
    }

private:
    int _tickCounter = 0;
    double _elapsed = 0.0;
};

// 2. Background Signal Producer Service
class SignalProducerService : public BackgroundService<> {
public:
    void poll() {
        _signalCount++;
        if (_signalCount % 3 == 0) {
            postEvent(SignalEvent{static_cast<uint32_t>(_signalCount)});
        }
    }

private:
    int _signalCount = 0;
};

// 3. Application Managing Both Services via CRTP
class ServiceApp : public AppCoreT<ServiceApp, Runtime::EventBusType> {
public:
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
        std::cout << "[ServiceApp] All background services ready.\n";
    }

private:
    int _tickCount = 0;
};

int main() {
    std::cout << "========================================\n";
    std::cout << "Corium Sample 02: Multi-Producer Services\n";
    std::cout << "========================================\n";

    Runtime runtime;
    ServiceApp app;
    ServiceManager<SensorService, SignalProducerService> serviceManager;

    runtime.initialize(app);
    serviceManager.initialize(ServiceContext{runtime.eventSink()});

    while (!runtime.quitRequested()) {
        serviceManager.poll();
        runtime.pump();
    }

    serviceManager.shutdown();
    runtime.shutdown();
    std::cout << "[Main] Exit complete.\n";
    return 0;
}
