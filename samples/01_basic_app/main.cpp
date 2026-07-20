// =============================================================================
// Corium Sample 01 — Basic Application
//
// This example demonstrates:
//   1. Subclassing AppCore to define application logic.
//   2. Registering typed event handlers in onRegisterHandlers().
//   3. Initializing Runtime and driving the event loop via pump().
//   4. Gracefully stopping the event loop with requestQuit().
// =============================================================================

#include <corium/corium.hpp>
#include <iostream>

using namespace corium;

class BasicApp : public AppCore {
protected:
    void onRegisterHandlers() override {
        events().registerHandler<UpdateEvent>([this](const UpdateEvent& event) {
            _frameCount++;
            std::cout << "[BasicApp] Update frame #" << _frameCount 
                      << " (dt: " << event.deltaTime << "s)\n";

            if (_frameCount >= 5) {
                std::cout << "[BasicApp] Reached 5 frames, requesting quit.\n";
                requestQuit();
            }
        });

        events().registerHandler<QuitEvent>([](const QuitEvent&) {
            std::cout << "[BasicApp] QuitEvent received.\n";
        });
    }

    void onInitialize() override {
        std::cout << "[BasicApp] Initialized successfully.\n";
    }

    void onShutdown() override {
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
