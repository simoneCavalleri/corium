#pragma once

#include <cstdint>

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#define CORIUM_HAS_ESP32_CRITICAL 1
#elif defined(__arm__) || defined(__thumb__)
#if __has_include(<cmsis_compiler.h>)
#include <cmsis_compiler.h>
#define CORIUM_HAS_ARM_CMSIS 1
#endif
#endif

namespace corium::embedded {

/// @brief RAII interrupt locking helper disabling interrupts upon construction and restoring them upon destruction.
class InterruptLock {
public:
    InterruptLock() noexcept
    {
        lock();
    }

    ~InterruptLock() noexcept
    {
        unlock();
    }

    InterruptLock(const InterruptLock&) = delete;
    InterruptLock& operator=(const InterruptLock&) = delete;

    InterruptLock(InterruptLock&&) = delete;
    InterruptLock& operator=(InterruptLock&&) = delete;

    void lock() noexcept
    {
        if (!_locked) {
#if defined(CORIUM_HAS_ESP32_CRITICAL)
            portENTER_CRITICAL(&_mux);
#elif defined(CORIUM_HAS_ARM_CMSIS)
            _primask = __get_PRIMASK();
            __disable_irq();
#endif
            _locked = true;
        }
    }

    void unlock() noexcept
    {
        if (_locked) {
#if defined(CORIUM_HAS_ESP32_CRITICAL)
            portEXIT_CRITICAL(&_mux);
#elif defined(CORIUM_HAS_ARM_CMSIS)
            __set_PRIMASK(_primask);
#endif
            _locked = false;
        }
    }

    [[nodiscard]] bool isLocked() const noexcept
    {
        return _locked;
    }

private:
    bool _locked = false;
#if defined(CORIUM_HAS_ESP32_CRITICAL)
    static inline portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
#elif defined(CORIUM_HAS_ARM_CMSIS)
    uint32_t _primask = 0;
#endif
};

} // namespace corium::embedded
