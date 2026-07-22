// =============================================================================
// Corium Sample 05 — ESP32 / ESP-IDF FreeRTOS Integration
//
// Demonstrates:
//   1. Zero-heap, zero-RTTI MPSC event processing on ESP32 with ESP-IDF.
//   2. Non-allocating lock-free event posting directly from a GPIO ISR handler.
//   3. Configurable GPIO PIN (default: GPIO_NUM_27, customizable).
//   4. Embedded real-time policy configuration via RuntimeBuilder.
// =============================================================================

#include <corium/corium.hpp>

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
// Fallback definitions for host compilation and desktop testing
enum gpio_num_t { GPIO_NUM_27 = 27 };
enum gpio_int_type_t { GPIO_INTR_NEGEDGE = 0 };
enum gpio_mode_t { GPIO_MODE_INPUT = 0 };
enum gpio_pulldown_t { GPIO_PULLDOWN_DISABLE = 0 };
enum gpio_pullup_t { GPIO_PULLUP_ENABLE = 1 };

struct gpio_config_t {
    gpio_int_type_t intr_type;
    gpio_mode_t mode;
    uint64_t pin_bit_mask;
    gpio_pulldown_t pull_down_en;
    gpio_pullup_t pull_up_en;
};

inline void gpio_config(const gpio_config_t*) {}
inline void gpio_install_isr_service(int) {}
inline void gpio_isr_handler_add(gpio_num_t, void (*)(void*), void*) {}

#define pdMS_TO_TICKS(ms) (ms)
inline void vTaskDelay(uint32_t) {}
inline void vTaskDelete(void*) {}
inline void xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t, void*, uint32_t, void*, int) {}
#endif

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#include <cstdint>
#include <iostream>

using namespace corium;

// -----------------------------------------------------------------------------
// 1. ESP32 Hardware Button PIN Configuration
// -----------------------------------------------------------------------------
// Default button GPIO pin is GPIO_NUM_27. Change this constant to any valid
// GPIO pin on your ESP32 board (e.g. GPIO_NUM_0 for the built-in BOOT button,
// GPIO_NUM_4, GPIO_NUM_12, etc.).
static constexpr gpio_num_t BUTTON_GPIO = GPIO_NUM_27;

// -----------------------------------------------------------------------------
// 2. Custom Embedded Event Variant List
// -----------------------------------------------------------------------------

/// @brief Event triggered by GPIO hardware button interrupt service routine (ISR)
struct ButtonPressEvent {
    uint8_t pin;
    uint32_t pressDurationMs;
};

/// @brief Custom variant list of events supported by this ESP32 firmware
using Esp32Events = std::variant<
    QuitEvent,
    TickEvent,
    ButtonPressEvent
>;

// -----------------------------------------------------------------------------
// 3. Embedded Policy-Based Runtime Configuration
// -----------------------------------------------------------------------------
// On ESP32, SRAM memory is valuable. We configure a compact ring buffer (256 items),
// sub-microsecond busy-spin polling (NoSignalPolicy), and compact 16-byte inline storage.
using Esp32Runtime = RuntimeBuilder<>
    ::WithEvents<Esp32Events>
    ::WithCapacity<256>                            // 256-element lock-free ring buffer
    ::WithSignalPolicy<NoSignalPolicy>            // Sub-microsecond real-time latency
    ::WithStoragePolicy<CompactStoragePolicy>     // 4 handlers per event, 16B inline
    ::Build;

// -----------------------------------------------------------------------------
// 4. ESP32 Firmware Application Core (CRTP Static Dispatch)
// -----------------------------------------------------------------------------
class Esp32FirmwareApp : public AppCoreT<Esp32FirmwareApp, Esp32Runtime::EventBusType> {
public:
    void onRegisterHandlers() {
        // Auto-deduces ButtonPressEvent from parameter type signature
        on([this](const ButtonPressEvent& e) {
            _pressCount++;
            std::cout << "[ESP32-APP] Button Press #" << _pressCount
                      << " detected on GPIO " << static_cast<int>(e.pin)
                      << " (Duration: " << e.pressDurationMs << "ms)\n";

            if (_pressCount >= 5) {
                std::cout << "[ESP32-APP] Reached 5 button presses. Shutting down runtime.\n";
                requestQuit();
            }
        });
    }

    void onInitialize() {
        std::cout << "[ESP32-APP] Firmware initialized. Listening for button ISR events on GPIO "
                  << static_cast<int>(BUTTON_GPIO) << "...\n";
    }

private:
    uint32_t _pressCount = 0;
};

// -----------------------------------------------------------------------------
// 5. Global Runtime & Hardware Event Sink Objects
// -----------------------------------------------------------------------------
static Esp32Runtime g_runtime;
static Esp32FirmwareApp g_app;

using EventSinkType = decltype(g_runtime.eventSink());
static EventSinkType g_sink;

// Hardware ISR handler executed in interrupt context (IRAM) on falling edge of button GPIO
static void IRAM_ATTR gpio_button_isr_handler(void* arg) {
    auto sinkPtr = static_cast<EventSinkType*>(arg);
    // Thread-safe lock-free MPSC push directly from ISR context (0 heap allocations)
    sinkPtr->post(ButtonPressEvent{static_cast<uint8_t>(BUTTON_GPIO), 42});
}

// Configure ESP32 GPIO pin for button input with pull-up resistor & falling-edge interrupt
static void init_button_gpio(EventSinkType* sink) {
    gpio_config_t io_conf{};
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1ULL << static_cast<uint64_t>(BUTTON_GPIO);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, gpio_button_isr_handler, sink);
}

// FreeRTOS Task running on Core 1 acting as Corium Single-Consumer event pump
static void runtime_task(void* pvParameters) {
    auto runtime = static_cast<Esp32Runtime*>(pvParameters);

    while (!runtime->quitRequested()) {
        runtime->pump();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    runtime->shutdown();
    std::cout << "[ESP32-APP] Runtime loop terminated cleanly.\n";
    vTaskDelete(nullptr);
}

// -----------------------------------------------------------------------------
// 6. ESP-IDF Main Entry Point (app_main)
// -----------------------------------------------------------------------------
extern "C" void app_main(void) {
    std::cout << "========================================\n";
    std::cout << "Corium Sample 05: ESP32 Button Interrupt\n";
    std::cout << "========================================\n";

    g_runtime.initialize(g_app);
    g_sink = g_runtime.eventSink();

    // Initialize hardware GPIO interrupt service
    init_button_gpio(&g_sink);

    // Create FreeRTOS Task pinned to Core 1 for Corium Event Consumer
    xTaskCreatePinnedToCore(runtime_task, "runtime_task", 8192, &g_runtime, 1, nullptr, 1);
}

#ifndef ESP_PLATFORM
int main() {
    app_main();
    return 0;
}
#endif
