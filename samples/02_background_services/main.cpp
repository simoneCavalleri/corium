// =============================================================================
// Corium Sample 02 — Multi-Threaded Background Services
//
// This example demonstrates:
//   1. Registering multiple BackgroundService instances in ServiceRegistry.
//   2. Asynchronous thread producers writing concurrently to the Lock-Free MPSC queue.
//   3. Auto-deduced lambda event handlers via on(...).
//   4. Power-saving main event loop using runtime.waitAndPump(timeout).
//   5. Graceful thread shutdown via std::stop_token.
// =============================================================================

#include <corium/corium.hpp>
#include <chrono>
#include <iostream>

using namespace corium;

// 1. Background Sensor Service
class SensorService : public BackgroundService {
public:
    using BackgroundService::BackgroundService;

    void run(std::stop_token stopToken) override {
        double elapsed = 0.0;
        while (!stopToken.stop_requested()) {
            postEvent(TickEvent{elapsed});
            elapsed += 0.2;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
};

// 2. Background Input Simulation Service
class InputSimulationService : public BackgroundService {
public:
    using BackgroundService::BackgroundService;

    void run(std::stop_token stopToken) override {
        int clickCount = 0;
        while (!stopToken.stop_requested()) {
            clickCount++;
            postEvent(MouseClickEvent{clickCount * 10, clickCount * 15, true, false});
            std::this_thread::sleep_for(std::chrono::milliseconds(350));
        }
    }
};

// 3. Application Managing Both Services
class ServiceApp : public AppCore {
protected:
    void onConfigureServices(ServiceRegistry& registry) override {
        registry.addService<SensorService>();
        registry.addService<InputSimulationService>();
    }

    void onRegisterHandlers() override {
        on([this](const TickEvent& e) {
            _tickCount++;
            std::cout << "[ServiceApp] Sensor Tick #" << _tickCount << " (time: " << e.deltaTime << "s)\n";
        });

        on([this](const MouseClickEvent& e) {
            std::cout << "[ServiceApp] Simulated MouseClick at (" << e.x << ", " << e.y << ")\n";
            if (_tickCount >= 5) {
                std::cout << "[ServiceApp] Processed 5 ticks, requesting quit...\n";
                requestQuit();
            }
        });
    }

    void onInitialize() override {
        std::cout << "[ServiceApp] All background services started.\n";
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

    runtime.initialize(app);

    // Clean, zero-boilerplate event loop using waitAndPump(timeout)
    while (!runtime.quitRequested()) {
        runtime.waitAndPump(std::chrono::milliseconds(50));
    }

    runtime.shutdown();
    std::cout << "[Main] Exit complete.\n";
    return 0;
}
