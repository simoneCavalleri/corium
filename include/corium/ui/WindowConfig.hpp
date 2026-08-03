#pragma once

#include <cstdint>

namespace corium::ui {

/// @brief Configuration settings for creating a Desktop Window.
struct WindowConfig {
    const char* title = "Corium Application";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool vsync = true;
    bool resizable = true;
    bool fullscreen = false;
    bool noApi = false;
};

} // namespace corium::ui
