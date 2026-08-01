#pragma once

#include "corium/AppCore.hpp"
#include "corium/ui/Window.hpp"

namespace corium::ui {

/// @brief High-level CRTP base class for single-window desktop applications.
/// Inherits from AppCore<Derived, EventBusType> and embeds a BasicWindow<WindowBackend>.
/// Automatically registers WindowCloseEvent handler to trigger requestQuit(),
/// and provides onRender() hook on each frame cycle.
template <
    typename Derived,
    typename WindowType = BasicWindow<backends::NullWindowBackend>,
    typename EventBusType = EventBus
>
class WindowApp : public AppCoreT<Derived, EventBusType> {
public:
    using BaseApp = AppCoreT<Derived, EventBusType>;

    explicit WindowApp(const WindowConfig& config = WindowConfig{})
        : _windowConfig(config)
    {}

    /// @brief Access embedded window instance.
    [[nodiscard]] WindowType& window() noexcept { return _window; }
    [[nodiscard]] const WindowType& window() const noexcept { return _window; }

    /// @brief Access window configuration.
    [[nodiscard]] const WindowConfig& windowConfig() const noexcept { return _windowConfig; }

    void onSetContext(AppCoreContextT<EventBusType>)
    {
        // 1. Automatically register WindowCloseEvent -> requestQuit()
        BaseApp::on([this](const WindowCloseEvent&) {
            BaseApp::requestQuit();
        });

        // 2. Automatically create window and bind event sink
        _window.create(_windowConfig, BaseApp::eventSink());
        _lastFrameTime = std::chrono::steady_clock::now();
    }

    void shutdown()
    {
        if constexpr (requires(Derived& d) { d.onShutdown(); }) {
            static_cast<Derived*>(this)->onShutdown();
        }
        _window.close();
    }

    /// @brief Frame render hook executed every iteration after event pump.
    void render()
    {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - _lastFrameTime).count();
        _lastFrameTime = now;

        if constexpr (requires(Derived& d, double deltaTime) { d.onRender(deltaTime); }) {
            static_cast<Derived*>(this)->onRender(dt);
        } else if constexpr (requires(Derived& d) { d.onRender(); }) {
            static_cast<Derived*>(this)->onRender();
        }
        _window.swapBuffers();
    }

    /// @brief Convenient main loop helper driving desktop event polling, pump, and render.
    template <typename RuntimeType>
    void run(RuntimeType& runtime)
    {
        while (!runtime.quitRequested() && !_window.shouldClose()) {
            _window.pollEvents();
            runtime.pump();
            render();
        }
    }

private:
    WindowConfig _windowConfig{};
    WindowType _window{};
    std::chrono::steady_clock::time_point _lastFrameTime{};
};

} // namespace corium::ui
