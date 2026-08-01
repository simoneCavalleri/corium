#include <corium/corium.hpp>
#include <corium/ui/ui.hpp>
#include <iostream>

using namespace corium;
using namespace corium::ui;

using AppEvents = std::variant<
    QuitEvent,
    WindowResizeEvent,
    MouseMoveEvent,
    MouseButtonEvent,
    KeyEvent,
    WindowCloseEvent
>;

using RealDesktopRuntime = RuntimeBuilder<>::WithEvents<AppEvents>::Build;

class RealGlfwApp : public WindowApp<RealGlfwApp, GlfwWindow, RealDesktopRuntime::EventBusType> {
public:
    RealGlfwApp()
        : WindowApp(WindowConfig{"Corium Real GLFW Window", 1024, 768})
    {}

    void onRegisterHandlers()
    {
        on([](const WindowResizeEvent& e) {
            std::cout << "[RealGlfwApp] Window resized to " << e.width << "x" << e.height << std::endl;
        });

        on([](const MouseMoveEvent& e) {
            std::cout << "[RealGlfwApp] Mouse position: (" << e.x << ", " << e.y << ")" << std::endl;
        });

        on([](const KeyEvent& e) {
            if (e.pressed) {
                std::cout << "[RealGlfwApp] Key pressed: " << e.key << " (scancode: " << e.scancode << ")" << std::endl;
            }
        });
    }

    void onInitialize()
    {
        std::cout << "[RealGlfwApp] Real OS GLFW Window created successfully!" << std::endl;
    }

    void onRender(double dt)
    {
        _frameCount++;
        if (_frameCount % 120 == 0) {
            std::cout << "[RealGlfwApp] Rendering frame #" << _frameCount << " (dt: " << (dt * 1000.0) << " ms)" << std::endl;
        }
    }

private:
    uint64_t _frameCount = 0;
};

int main()
{
    std::cout << "=========================================================\n";
    std::cout << " Corium Sample 10: Real Native GLFW Desktop Window\n";
    std::cout << "=========================================================\n" << std::endl;

    RealDesktopRuntime runtime;
    RealGlfwApp app;

    runtime.initialize(app);

    if (app.window().shouldClose()) {
        std::cerr << "[RealGlfwApp Error] Could not open GLFW window on current display server!" << std::endl;
        return 1;
    }

    std::cout << "Running Real GLFW Window main loop (Move mouse or press keys inside window)..." << std::endl;
    app.run(runtime);

    runtime.shutdown();
    std::cout << "\nSample 10 complete." << std::endl;
    return 0;
}
