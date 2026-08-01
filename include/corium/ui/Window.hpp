#pragma once

#include "corium/ui/WindowConfig.hpp"
#include "corium/ui/WindowEvents.hpp"
#include "corium/ui/backends/NullWindowBackend.hpp"
#include "corium/ui/backends/GlfwWindowBackend.hpp"

namespace corium::ui {

/// @brief Generic desktop window wrapper using static backend policy dispatch (zero vtables).
/// @tparam BackendPolicy Windowing backend implementation (e.g. GlfwWindowBackend, NullWindowBackend).
template <typename BackendPolicy = backends::NullWindowBackend>
class BasicWindow {
public:
    BasicWindow() = default;

    /// @brief Create window and bind OS callbacks to target Corium IEventSink.
    template <typename EventSink>
    bool create(const WindowConfig& config, EventSink sink)
    {
        return _backend.create(config, sink);
    }

    /// @brief Poll native windowing OS messages.
    void pollEvents() noexcept
    {
        _backend.pollEvents();
    }

    /// @brief Swap front/back render buffers.
    void swapBuffers() noexcept
    {
        _backend.swapBuffers();
    }

    /// @brief Check if window close has been requested.
    [[nodiscard]] bool shouldClose() const noexcept
    {
        return _backend.shouldClose();
    }

    /// @brief Close and destroy native window handle.
    void close() noexcept
    {
        _backend.close();
    }

    /// @brief Access underlying backend policy instance.
    [[nodiscard]] BackendPolicy& backend() noexcept { return _backend; }
    [[nodiscard]] const BackendPolicy& backend() const noexcept { return _backend; }

private:
    BackendPolicy _backend{};
};

/// @brief Headless mock window alias for unit testing.
using NullWindow = BasicWindow<backends::NullWindowBackend>;

/// @brief GLFW desktop window alias.
using GlfwWindow = BasicWindow<backends::GlfwWindowBackend>;

} // namespace corium::ui
