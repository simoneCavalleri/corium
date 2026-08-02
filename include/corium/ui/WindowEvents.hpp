#pragma once

#include <cstdint>

namespace corium::ui {

/// @brief Event emitted when the window is resized by the user or OS.
struct WindowResizeEvent {
    int width = 0;
    int height = 0;
};

/// @brief Event emitted when the window is moved on screen.
struct WindowMoveEvent {
    int x = 0;
    int y = 0;
};

/// @brief Event emitted when the window framebuffer is resized (e.g. HiDPI/Retina displays).
struct FramebufferResizeEvent {
    int width = 0;
    int height = 0;
};

/// @brief Event emitted when the window is iconified/minimized or restored.
struct WindowMinimizeEvent {
    bool minimized = false;
};

/// @brief Event emitted when the window is maximized or un-maximized.
struct WindowMaximizeEvent {
    bool maximized = false;
};

/// @brief Event emitted when window focus state changes.
struct WindowFocusEvent {
    bool focused = false;
};

/// @brief Event emitted when window contents need redrawing.
struct WindowRefreshEvent {};

/// @brief Event emitted when the monitor DPI/content scale changes.
struct WindowContentScaleEvent {
    float xscale = 1.0f;
    float yscale = 1.0f;
};

/// @brief Event emitted when the window close button is clicked or close is requested.
struct WindowCloseEvent {};

/// @brief Event emitted when the mouse cursor is moved inside the window.
struct MouseMoveEvent {
    float x = 0.0f;
    float y = 0.0f;
};

/// @brief Event emitted when a mouse button is pressed or released.
struct MouseButtonEvent {
    int button = 0;
    bool pressed = false;
    float x = 0.0f;
    float y = 0.0f;
};

/// @brief Event emitted when the mouse wheel or touchpad is scrolled.
struct MouseScrollEvent {
    float xoffset = 0.0f;
    float yoffset = 0.0f;
};

/// @brief Event emitted when the cursor enters or leaves the window bounds.
struct MouseEnterEvent {
    bool entered = false;
};

/// @brief Event emitted when a key is pressed, released, or repeated.
struct KeyEvent {
    int key = 0;
    int scancode = 0;
    bool pressed = false;
    bool repeat = false;
};

/// @brief Event emitted for Unicode character text input.
struct CharEvent {
    uint32_t codepoint = 0;
};

} // namespace corium::ui
