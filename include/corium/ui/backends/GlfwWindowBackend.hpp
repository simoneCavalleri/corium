#pragma once

#include <iostream>
#include <cstring>
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
        _sinkHolder = rhs._sinkHolder;
        rhs._window = nullptr;
        rhs._sinkHolder.active = false;
        if (_window) {
            glfwSetWindowUserPointer(_window, &_sinkHolder);
        }
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
            _sinkHolder = rhs._sinkHolder;
            rhs._window = nullptr;
            rhs._sinkHolder.active = false;
            if (_window) {
                glfwSetWindowUserPointer(_window, &_sinkHolder);
            }
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
            std::cerr << "[Corium GLFW Error] Failed to initialize GLFW library!\n" << std::flush;
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
            const char* description = nullptr;
            int errorCode = glfwGetError(&description);
            std::cerr << "[Corium GLFW Error] Failed to create GLFW window! (Error "
                      << errorCode << ": " << (description ? description : "Unknown error") << ")\n" << std::flush;
            return false;
        }

        glfwMakeContextCurrent(_window);
        glfwSwapInterval(config.vsync ? 1 : 0);

        static_assert(sizeof(EventSink) <= sizeof(_sinkHolder.sinkStorage), "EventSink size exceeds GlfwWindowBackend inline storage size!");

        _sinkHolder.postMove = [](void* sinkPtr, WindowMoveEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(std::move(evt));
        };
        _sinkHolder.postResize = [](void* sinkPtr, WindowResizeEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(std::move(evt));
        };
        _sinkHolder.postFramebufferResize = [](void* sinkPtr, FramebufferResizeEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(std::move(evt));
        };
        _sinkHolder.postMinimize = [](void* sinkPtr, WindowMinimizeEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(std::move(evt));
        };
        _sinkHolder.postMaximize = [](void* sinkPtr, WindowMaximizeEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(std::move(evt));
        };
        _sinkHolder.postFocus = [](void* sinkPtr, WindowFocusEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(std::move(evt));
        };
        _sinkHolder.postRefresh = [](void* sinkPtr, WindowRefreshEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(std::move(evt));
        };
        _sinkHolder.postContentScale = [](void* sinkPtr, WindowContentScaleEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(std::move(evt));
        };
        _sinkHolder.postClose = [](void* sinkPtr, WindowCloseEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(std::move(evt));
        };
        _sinkHolder.postMouseMove = [](void* sinkPtr, MouseMoveEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(std::move(evt));
        };
        _sinkHolder.postMouseButton = [](void* sinkPtr, MouseButtonEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(std::move(evt));
        };
        _sinkHolder.postMouseScroll = [](void* sinkPtr, MouseScrollEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(std::move(evt));
        };
        _sinkHolder.postMouseEnter = [](void* sinkPtr, MouseEnterEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(std::move(evt));
        };
        _sinkHolder.postKey = [](void* sinkPtr, KeyEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(std::move(evt));
        };
        _sinkHolder.postChar = [](void* sinkPtr, CharEvent evt) {
            static_cast<EventSink*>(sinkPtr)->post(std::move(evt));
        };

        std::memcpy(_sinkHolder.sinkStorage, &sink, sizeof(EventSink));
        _sinkHolder.active = true;

        glfwSetWindowUserPointer(_window, &_sinkHolder);

        // Bind GLFW Callbacks
        glfwSetWindowPosCallback(_window, [](GLFWwindow* win, int xpos, int ypos) {
            auto holder = static_cast<StaticSinkHolder*>(glfwGetWindowUserPointer(win));
            if (holder && holder->postMove && holder->active) {
                holder->postMove(holder->sinkStorage, WindowMoveEvent{xpos, ypos});
            }
        });

        glfwSetWindowSizeCallback(_window, [](GLFWwindow* win, int width, int height) {
            auto holder = static_cast<StaticSinkHolder*>(glfwGetWindowUserPointer(win));
            if (holder && holder->postResize && holder->active) {
                holder->postResize(holder->sinkStorage, WindowResizeEvent{width, height});
            }
        });

        glfwSetFramebufferSizeCallback(_window, [](GLFWwindow* win, int width, int height) {
            auto holder = static_cast<StaticSinkHolder*>(glfwGetWindowUserPointer(win));
            if (holder && holder->postFramebufferResize && holder->active) {
                holder->postFramebufferResize(holder->sinkStorage, FramebufferResizeEvent{width, height});
            }
        });

        glfwSetWindowIconifyCallback(_window, [](GLFWwindow* win, int iconified) {
            auto holder = static_cast<StaticSinkHolder*>(glfwGetWindowUserPointer(win));
            if (holder && holder->postMinimize && holder->active) {
                holder->postMinimize(holder->sinkStorage, WindowMinimizeEvent{iconified == GLFW_TRUE});
            }
        });

        glfwSetWindowMaximizeCallback(_window, [](GLFWwindow* win, int maximized) {
            auto holder = static_cast<StaticSinkHolder*>(glfwGetWindowUserPointer(win));
            if (holder && holder->postMaximize && holder->active) {
                holder->postMaximize(holder->sinkStorage, WindowMaximizeEvent{maximized == GLFW_TRUE});
            }
        });

        glfwSetWindowFocusCallback(_window, [](GLFWwindow* win, int focused) {
            auto holder = static_cast<StaticSinkHolder*>(glfwGetWindowUserPointer(win));
            if (holder && holder->postFocus && holder->active) {
                holder->postFocus(holder->sinkStorage, WindowFocusEvent{focused == GLFW_TRUE});
            }
        });

        glfwSetWindowRefreshCallback(_window, [](GLFWwindow* win) {
            auto holder = static_cast<StaticSinkHolder*>(glfwGetWindowUserPointer(win));
            if (holder && holder->postRefresh && holder->active) {
                holder->postRefresh(holder->sinkStorage, WindowRefreshEvent{});
            }
        });

        glfwSetWindowContentScaleCallback(_window, [](GLFWwindow* win, float xscale, float yscale) {
            auto holder = static_cast<StaticSinkHolder*>(glfwGetWindowUserPointer(win));
            if (holder && holder->postContentScale && holder->active) {
                holder->postContentScale(holder->sinkStorage, WindowContentScaleEvent{xscale, yscale});
            }
        });

        glfwSetWindowCloseCallback(_window, [](GLFWwindow* win) {
            auto holder = static_cast<StaticSinkHolder*>(glfwGetWindowUserPointer(win));
            if (holder && holder->postClose && holder->active) {
                holder->postClose(holder->sinkStorage, WindowCloseEvent{});
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

        glfwSetScrollCallback(_window, [](GLFWwindow* win, double xoffset, double yoffset) {
            auto holder = static_cast<StaticSinkHolder*>(glfwGetWindowUserPointer(win));
            if (holder && holder->postMouseScroll && holder->active) {
                holder->postMouseScroll(holder->sinkStorage, MouseScrollEvent{
                    static_cast<float>(xoffset),
                    static_cast<float>(yoffset)
                });
            }
        });

        glfwSetCursorEnterCallback(_window, [](GLFWwindow* win, int entered) {
            auto holder = static_cast<StaticSinkHolder*>(glfwGetWindowUserPointer(win));
            if (holder && holder->postMouseEnter && holder->active) {
                holder->postMouseEnter(holder->sinkStorage, MouseEnterEvent{entered == GLFW_TRUE});
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

        glfwSetCharCallback(_window, [](GLFWwindow* win, unsigned int codepoint) {
            auto holder = static_cast<StaticSinkHolder*>(glfwGetWindowUserPointer(win));
            if (holder && holder->postChar && holder->active) {
                holder->postChar(holder->sinkStorage, CharEvent{static_cast<uint32_t>(codepoint)});
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
            _sinkHolder.active = false;
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
        void (*postMove)(void*, WindowMoveEvent) = nullptr;
        void (*postResize)(void*, WindowResizeEvent) = nullptr;
        void (*postFramebufferResize)(void*, FramebufferResizeEvent) = nullptr;
        void (*postMinimize)(void*, WindowMinimizeEvent) = nullptr;
        void (*postMaximize)(void*, WindowMaximizeEvent) = nullptr;
        void (*postFocus)(void*, WindowFocusEvent) = nullptr;
        void (*postRefresh)(void*, WindowRefreshEvent) = nullptr;
        void (*postContentScale)(void*, WindowContentScaleEvent) = nullptr;
        void (*postClose)(void*, WindowCloseEvent) = nullptr;
        void (*postMouseMove)(void*, MouseMoveEvent) = nullptr;
        void (*postMouseButton)(void*, MouseButtonEvent) = nullptr;
        void (*postMouseScroll)(void*, MouseScrollEvent) = nullptr;
        void (*postMouseEnter)(void*, MouseEnterEvent) = nullptr;
        void (*postKey)(void*, KeyEvent) = nullptr;
        void (*postChar)(void*, CharEvent) = nullptr;
        bool active = false;
    };

#if CORIUM_HAS_GLFW
    GLFWwindow* _window = nullptr;
    StaticSinkHolder _sinkHolder{};
#endif
};

} // namespace corium::ui::backends
