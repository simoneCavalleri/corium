#pragma once

#include <variant>

namespace corium {

struct UpdateEvent {
    double deltaTime;

    explicit UpdateEvent(double dt = 0.0)
        : deltaTime(dt) {}
};

struct RenderEvent {
    double deltaTime;

    explicit RenderEvent(double dt = 0.0)
        : deltaTime(dt) {}
};

struct TickEvent {
    double deltaTime;

    explicit TickEvent(double dt = 0.0)
        : deltaTime(dt) {}
};

struct WindowResizeEvent {
    int width;
    int height;

    WindowResizeEvent(int newWidth, int newHeight)
        : width(newWidth), height(newHeight) {}
};

struct WindowCloseEvent {};

struct MouseClickEvent {
    int x;
    int y;
    bool leftButton;
    bool rightButton;

    MouseClickEvent(int xPos, int yPos, bool left, bool right)
        : x(xPos), y(yPos), leftButton(left), rightButton(right) {}
};

struct MousePressedEvent {
    int x;
    int y;
    int button;

    MousePressedEvent(int xPos, int yPos, int buttonCode)
        : x(xPos), y(yPos), button(buttonCode) {}
};

struct MouseReleasedEvent {
    int x;
    int y;
    int button;

    MouseReleasedEvent(int xPos, int yPos, int buttonCode)
        : x(xPos), y(yPos), button(buttonCode) {}
};

struct MouseMoveEvent {
    int x;
    int y;

    MouseMoveEvent(int xPos, int yPos)
        : x(xPos), y(yPos) {}
};

struct MouseScrollEvent {
    int delta;

    explicit MouseScrollEvent(int scrollDelta)
        : delta(scrollDelta) {}
};

struct KeyPressedEvent {
    int keyCode;

    explicit KeyPressedEvent(int code)
        : keyCode(code) {}
};

struct KeyReleasedEvent {
    int keyCode;

    explicit KeyReleasedEvent(int code)
        : keyCode(code) {}
};

struct QuitEvent {};

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

using Event = DefaultEvents;

} // namespace corium

