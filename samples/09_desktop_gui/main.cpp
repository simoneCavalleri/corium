#include <corium/corium.hpp>
#include <corium/ui/ui.hpp>
#include <iostream>

using namespace corium;
using namespace corium::ui;

// Combine standard events with Desktop UI events
using AppEvents = std::variant<
    QuitEvent,
    WindowResizeEvent,
    MouseMoveEvent,
    MouseButtonEvent,
    KeyEvent,
    WindowCloseEvent
>;

using DesktopRuntime = RuntimeBuilder<>
    ::WithEvents<AppEvents>
    ::Build;

// Application uses single-window CRTP WindowApp
class DesktopDemoApp : public WindowApp<DesktopDemoApp, NullWindow, DesktopRuntime::EventBusType> {
public:
    DesktopDemoApp()
        : WindowApp(WindowConfig{"Corium Sample 09 - Desktop GUI", 1280, 720})
    {}

    void onRegisterHandlers()
    {
        on([](const WindowResizeEvent& e) {
            std::cout << "[DesktopApp] Window resized to " << e.width << "x" << e.height << "\n";
        });

        on([](const MouseMoveEvent& e) {
            std::cout << "[DesktopApp] Mouse cursor position: (" << e.x << ", " << e.y << ")\n";
        });

        on([this](const KeyEvent& e) {
            if (e.pressed) {
                std::cout << "[DesktopApp] Key pressed: " << e.key << "\n";
                _keysPressed++;
                if (_keysPressed >= 3) {
                    std::cout << "[DesktopApp] Received 3 key presses, requesting quit...\n";
                    requestQuit();
                }
            }
        });
    }

    void onInitialize()
    {
        std::cout << "[DesktopApp] Window initialized and event handlers registered.\n";
    }

    void onRender(double dt)
    {
        (void)dt;
        _renderFrames++;
    }

    void onShutdown()
    {
        std::cout << "[DesktopApp] Shutdown complete. Total frames rendered: " << _renderFrames << "\n";
    }

private:
    int _keysPressed = 0;
    uint64_t _renderFrames = 0;
};

int main()
{
    std::cout << "=========================================================\n";
    std::cout << " Corium Sample 09: Desktop GUI Window Application\n";
    std::cout << "=========================================================\n\n";

    DesktopRuntime runtime;
    DesktopDemoApp app;

    runtime.initialize(app);

    std::cout << "Simulating desktop window user interactions...\n";
    auto sink = runtime.eventSink();

    // Simulate input events posted by windowing subsystem
    sink.post(WindowResizeEvent{1920, 1080});
    sink.post(MouseMoveEvent{640.0f, 360.0f});
    sink.post(KeyEvent{65, 30, true, false});  // 'A' key
    sink.post(KeyEvent{66, 31, true, false});  // 'B' key
    sink.post(KeyEvent{67, 32, true, false});  // 'C' key

    // Run Desktop GUI main loop cleanly via app.run(runtime)
    app.run(runtime);

    runtime.shutdown();
    std::cout << "\nSample 09 complete.\n";
    return 0;
}
