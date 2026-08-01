#pragma once

#include <cstdint>

namespace corium::ui {

/// @brief Event emitted when the window is resized by the user or OS.
struct WindowResizeEvent {
    int width = 0;
    int height = 0;
};

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

/// @brief Event emitted when a key is pressed, released, or repeated.
struct KeyEvent {
    int key = 0;
    int scancode = 0;
    bool pressed = false;
    bool repeat = false;
};

/// @brief Event emitted when window focus state changes.
struct WindowFocusEvent {
    bool focused = false;
};

/// @brief Event emitted when the window close button is clicked or close is requested.
struct WindowCloseEvent {};

} // namespace corium::ui
