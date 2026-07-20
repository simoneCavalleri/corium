#pragma once

#include <variant>

namespace corium {

/// @brief Event emitted for logical frame updates.
struct UpdateEvent {
    double deltaTime;

    explicit UpdateEvent(double dt = 0.0)
        : deltaTime(dt) {}
};

/// @brief Event emitted for rendering updates.
struct RenderEvent {
    double deltaTime;

    explicit RenderEvent(double dt = 0.0)
        : deltaTime(dt) {}
};

/// @brief Periodic heartbeat or timer tick event.
struct TickEvent {
    double deltaTime;

    explicit TickEvent(double dt = 0.0)
        : deltaTime(dt) {}
};

/// @brief Event emitted when application window is resized.
struct WindowResizeEvent {
    int width;
    int height;

    WindowResizeEvent(int newWidth, int newHeight)
        : width(newWidth), height(newHeight) {}
};

/// @brief Event emitted when window close button is pressed.
struct WindowCloseEvent {};

/// @brief Mouse click event.
struct MouseClickEvent {
    int x;
    int y;
    bool leftButton;
    bool rightButton;

    MouseClickEvent(int xPos, int yPos, bool left, bool right)
        : x(xPos), y(yPos), leftButton(left), rightButton(right) {}
};

/// @brief Mouse button press event.
struct MousePressedEvent {
    int x;
    int y;
    int button;

    MousePressedEvent(int xPos, int yPos, int buttonCode)
        : x(xPos), y(yPos), button(buttonCode) {}
};

/// @brief Mouse button release event.
struct MouseReleasedEvent {
    int x;
    int y;
    int button;

    MouseReleasedEvent(int xPos, int yPos, int buttonCode)
        : x(xPos), y(yPos), button(buttonCode) {}
};

/// @brief Mouse cursor move event.
struct MouseMoveEvent {
    int x;
    int y;

    MouseMoveEvent(int xPos, int yPos)
        : x(xPos), y(yPos) {}
};

/// @brief Mouse wheel scroll event.
struct MouseScrollEvent {
    int delta;

    explicit MouseScrollEvent(int scrollDelta)
        : delta(scrollDelta) {}
};

/// @brief Keyboard key press event.
struct KeyPressedEvent {
    int keyCode;

    explicit KeyPressedEvent(int code)
        : keyCode(code) {}
};

/// @brief Keyboard key release event.
struct KeyReleasedEvent {
    int keyCode;

    explicit KeyReleasedEvent(int code)
        : keyCode(code) {}
};

/// @brief Application shutdown event.
struct QuitEvent {};

/// @brief Default variant list of standard Corium events.
using DefaultEvents = std::variant<
    UpdateEvent,
    RenderEvent,
    TickEvent,
    WindowResizeEvent,
    WindowCloseEvent,
    MouseClickEvent,
    MousePressedEvent,
    MouseReleasedEvent,
    MouseMoveEvent,
    MouseScrollEvent,
    KeyPressedEvent,
    KeyReleasedEvent,
    QuitEvent>;

/// @brief Alias for backwards compatibility with DefaultEvents.
using Event = DefaultEvents;

} // namespace corium
