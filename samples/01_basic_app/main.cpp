// =============================================================================
// Corium Sample 01 — Basic Application (Bare-Metal CRTP)
//
// This example demonstrates:
//   1. Subclassing AppCoreT<BasicApp, EventBusType> using CRTP static dispatch.
//   2. Registering auto-deduced lambda event handlers via on(...).
//   3. Initializing zero-heap Runtime and driving the event loop via pump().
//   4. Gracefully stopping the event loop with requestQuit().
// =============================================================================

#include <corium/corium.hpp>
#include <iostream>

using namespace corium;

class BasicApp : public AppCoreT<BasicApp, Runtime::EventBusType> {
public:
    void onRegisterHandlers() {
        // Auto-deduces UpdateEvent from lambda argument signature
        on([this](const UpdateEvent& event) {
            _frameCount++;
            std::cout << "[BasicApp] Update frame #" << _frameCount 
                      << " (dt: " << event.deltaTime << "s)\n";

            if (_frameCount >= 5) {
                std::cout << "[BasicApp] Reached 5 frames, requesting quit.\n";
                requestQuit();
            }
        });

        // Auto-deduces QuitEvent from lambda argument signature
        on([](const QuitEvent&) {
            std::cout << "[BasicApp] QuitEvent received.\n";
        });
    }

    void onInitialize() {
        std::cout << "[BasicApp] Initialized successfully.\n";
    }

    void onShutdown() {
        std::cout << "[BasicApp] Shutdown complete.\n";
    }

private:
    int _frameCount = 0;
};

int main() {
    std::cout << "========================================\n";
    std::cout << "Corium Sample 01: Basic Application\n";
    std::cout << "========================================\n";

    Runtime runtime;
    BasicApp app;

    runtime.initialize(app);

    // Simulate main loop pushing update events and pumping runtime
    while (!runtime.quitRequested()) {
        runtime.eventSink().post(UpdateEvent{0.016}); // ~60 FPS dt
        runtime.pump();
    }

    runtime.shutdown();
    return 0;
}
