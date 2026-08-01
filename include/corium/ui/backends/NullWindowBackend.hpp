#pragma once

#include "corium/ui/WindowConfig.hpp"
#include "corium/ui/WindowEvents.hpp"
#include <cstdint>

namespace corium::ui::backends {

/// @brief Headless mock window backend for unit testing and headless CI.
class NullWindowBackend {
public:
    NullWindowBackend() = default;

    template <typename EventSink>
    bool create(const WindowConfig& config, EventSink sink)
    {
        (void)sink;
        _config = config;
        _isOpen = true;
        _shouldClose = false;
        return true;
    }

    void pollEvents() noexcept
    {
        // No-op for headless mock
    }

    void swapBuffers() noexcept
    {
        _frameCount++;
    }

    [[nodiscard]] bool shouldClose() const noexcept
    {
        return _shouldClose;
    }

    void requestClose() noexcept
    {
        _shouldClose = true;
    }

    void close() noexcept
    {
        _isOpen = false;
        _shouldClose = true;
    }

    [[nodiscard]] bool isOpen() const noexcept { return _isOpen; }
    [[nodiscard]] uint64_t frameCount() const noexcept { return _frameCount; }
    [[nodiscard]] const WindowConfig& config() const noexcept { return _config; }

private:
    WindowConfig _config{};
    bool _isOpen = false;
    bool _shouldClose = false;
    uint64_t _frameCount = 0;
};

} // namespace corium::ui::backends
