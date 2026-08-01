#pragma once

#include "corium/ui/WindowConfig.hpp"
#include "corium/ui/WindowEvents.hpp"

#if __has_include(<GLFW/glfw3.h>)
#include <GLFW/glfw3.h>
#define CORIUM_HAS_GLFW 1
#endif

#include <cstdint>

namespace corium::ui::backends {

/// @brief GLFW windowing backend bridging OS input and window callbacks to Corium's IEventSink.
class GlfwWindowBackend {
public:
    GlfwWindowBackend() = default;

    ~GlfwWindowBackend()
    {
        close();
    }

    GlfwWindowBackend(const GlfwWindowBackend&) = delete;
    GlfwWindowBackend& operator=(const GlfwWindowBackend&) = delete;

    GlfwWindowBackend(GlfwWindowBackend&& rhs) noexcept
    {
#if CORIUM_HAS_GLFW
        _window = rhs._window;
        rhs._window = nullptr;
#else
        (void)rhs;
#endif
    }

    GlfwWindowBackend& operator=(GlfwWindowBackend&& rhs) noexcept
    {
        if (this != &rhs) {
            close();
#if CORIUM_HAS_GLFW
            _window = rhs._window;
            rhs._window = nullptr;
#else
            (void)rhs;
#endif
        }
        return *this;
    }

    template <typename EventSink>
    bool create(const WindowConfig& config, EventSink sink)
    {
#if CORIUM_HAS_GLFW
        if (!glfwInit()) {
            return false;
        }

        glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

        GLFWmonitor* monitor = config.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
        _window = glfwCreateWindow(
            static_cast<int>(config.width),
            static_cast<int>(config.height),
            config.title,
            monitor,
            nullptr
        );

        if (!_window) {
            return false;
        }

        glfwMakeContextCurrent(_window);
        glfwSwapInterval(config.vsync ? 1 : 0);

        static_assert(sizeof(EventSink) <= 64, "EventSink size exceeds GlfwWindowBackend inline storage size!");

        _sinkHolder.postResize = [](void* sinkPtr, WindowResizeEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(evt);
        };
        _sinkHolder.postMouseMove = [](void* sinkPtr, MouseMoveEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(evt);
        };
        _sinkHolder.postMouseButton = [](void* sinkPtr, MouseButtonEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(evt);
        };
        _sinkHolder.postKey = [](void* sinkPtr, KeyEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(evt);
        };
        _sinkHolder.postClose = [](void* sinkPtr, WindowCloseEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(evt);
        };
        _sinkHolder.destroySink = [](void* sinkPtr) noexcept {
            static_cast<EventSink*>(sinkPtr)->~EventSink();
        };

        // Construct EventSink in-place into inline storage (0 heap allocations)
        ::new (static_cast<void*>(_sinkHolder.sinkStorage)) EventSink(sink);
        _sinkHolder.active = true;

        glfwSetWindowUserPointer(_window, &_sinkHolder);

        // Bind GLFW Callbacks
        glfwSetWindowSizeCallback(_window, [](GLFWwindow* win, int width, int height) {
            auto holder = static_cast<StaticSinkHolder*>(glfwGetWindowUserPointer(win));
            if (holder && holder->postResize && holder->active) {
                holder->postResize(holder->sinkStorage, WindowResizeEvent{width, height});
            }
        });

        glfwSetCursorPosCallback(_window, [](GLFWwindow* win, double xpos, double ypos) {
            auto holder = static_cast<StaticSinkHolder*>(glfwGetWindowUserPointer(win));
            if (holder && holder->postMouseMove && holder->active) {
                holder->postMouseMove(holder->sinkStorage, MouseMoveEvent{static_cast<float>(xpos), static_cast<float>(ypos)});
            }
        });

        glfwSetMouseButtonCallback(_window, [](GLFWwindow* win, int button, int action, int mods) {
            (void)mods;
            double xpos = 0, ypos = 0;
            glfwGetCursorPos(win, &xpos, &ypos);
            auto holder = static_cast<StaticSinkHolder*>(glfwGetWindowUserPointer(win));
            if (holder && holder->postMouseButton && holder->active) {
                holder->postMouseButton(holder->sinkStorage, MouseButtonEvent{
                    button,
                    action == GLFW_PRESS,
                    static_cast<float>(xpos),
                    static_cast<float>(ypos)
                });
            }
        });

        glfwSetKeyCallback(_window, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
            (void)mods;
            auto holder = static_cast<StaticSinkHolder*>(glfwGetWindowUserPointer(win));
            if (holder && holder->postKey && holder->active) {
                holder->postKey(holder->sinkStorage, KeyEvent{
                    key,
                    scancode,
                    action != GLFW_RELEASE,
                    action == GLFW_REPEAT
                });
            }
        });

        glfwSetWindowCloseCallback(_window, [](GLFWwindow* win) {
            auto holder = static_cast<StaticSinkHolder*>(glfwGetWindowUserPointer(win));
            if (holder && holder->postClose && holder->active) {
                holder->postClose(holder->sinkStorage, WindowCloseEvent{});
            }
        });

        return true;
#else
        (void)config;
        (void)sink;
        return false;
#endif
    }

    void pollEvents() noexcept
    {
#if CORIUM_HAS_GLFW
        glfwPollEvents();
#endif
    }

    void swapBuffers() noexcept
    {
#if CORIUM_HAS_GLFW
        if (_window) {
            glfwSwapBuffers(_window);
        }
#endif
    }

    [[nodiscard]] bool shouldClose() const noexcept
    {
#if CORIUM_HAS_GLFW
        return _window ? glfwWindowShouldClose(_window) != 0 : true;
#else
        return true;
#endif
    }

    void close() noexcept
    {
#if CORIUM_HAS_GLFW
        if (_window) {
            if (_sinkHolder.active && _sinkHolder.destroySink) {
                _sinkHolder.destroySink(_sinkHolder.sinkStorage);
                _sinkHolder.active = false;
            }
            glfwDestroyWindow(_window);
            _window = nullptr;
            glfwTerminate();
        }
#endif
    }

#if CORIUM_HAS_GLFW
    [[nodiscard]] GLFWwindow* nativeHandle() const noexcept { return _window; }
#endif

private:
    struct StaticSinkHolder {
        alignas(alignof(std::max_align_t)) std::byte sinkStorage[64]{};
        void (*postResize)(void*, WindowResizeEvent) = nullptr;
        void (*postMouseMove)(void*, MouseMoveEvent) = nullptr;
        void (*postMouseButton)(void*, MouseButtonEvent) = nullptr;
        void (*postKey)(void*, KeyEvent) = nullptr;
        void (*postClose)(void*, WindowCloseEvent) = nullptr;
        void (*destroySink)(void*) noexcept = nullptr;
        bool active = false;
    };

#if CORIUM_HAS_GLFW
    GLFWwindow* _window = nullptr;
    StaticSinkHolder _sinkHolder{};
#endif
};

} // namespace corium::ui::backends
