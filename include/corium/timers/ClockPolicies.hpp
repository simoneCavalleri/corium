#pragma once

#include <chrono>
#include <cstdint>
#include <type_traits>

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <esp_timer.h>
#define CORIUM_NATIVE_ESP_TIMER 1
#endif

#if defined(FREERTOS) || defined(INC_FREERTOS_H)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#define CORIUM_NATIVE_FREERTOS 1
#endif

namespace corium {

class ChronoClockPolicy;

namespace internal {

template <typename T, typename = void>
struct get_timer_clock_policy {
    using type = ChronoClockPolicy;
};

template <typename T>
struct get_timer_clock_policy<T, std::void_t<typename T::clock_policy>> {
    using type = typename T::clock_policy;
};

} // namespace internal

/// @brief Default host clock policy using std::chrono::steady_clock.
class ChronoClockPolicy {
public:
    using time_point = std::chrono::steady_clock::time_point;
    using duration = std::chrono::microseconds;

    [[nodiscard]] static time_point now() noexcept
    {
        return std::chrono::steady_clock::now();
    }

    template <typename Rep, typename Period>
    [[nodiscard]] static time_point add(time_point tp, const std::chrono::duration<Rep, Period>& d) noexcept
    {
        return tp + std::chrono::duration_cast<std::chrono::steady_clock::duration>(d);
    }

    [[nodiscard]] static bool isDue(time_point now, time_point expiry) noexcept
    {
        return now >= expiry;
    }
};

/// @brief Deterministic, manually-advanced clock policy for unit tests and simulation.
class ManualClockPolicy {
public:
    using time_point = uint64_t;
    using duration = uint64_t;

    [[nodiscard]] static time_point now() noexcept
    {
        return _currentTime;
    }

    static void set(time_point t) noexcept
    {
        _currentTime = t;
    }

    static void advance(duration delta) noexcept
    {
        _currentTime += delta;
    }

    static void reset() noexcept
    {
        _currentTime = 0;
    }

    template <typename Rep, typename Period>
    [[nodiscard]] static time_point add(time_point tp, const std::chrono::duration<Rep, Period>& d) noexcept
    {
        return tp + static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(d).count());
    }

    [[nodiscard]] static time_point add(time_point tp, duration delta) noexcept
    {
        return tp + delta;
    }

    [[nodiscard]] static bool isDue(time_point now, time_point expiry) noexcept
    {
        return now >= expiry;
    }

private:
    static inline time_point _currentTime = 0;
};

/// @brief Clock policy driven by a custom microsecond tick provider.
/// @tparam Provider Struct with static `uint64_t nowUs()` method.
template <typename Provider>
class MicrosecondTickClockPolicy {
public:
    using time_point = uint64_t;
    using duration = uint64_t;

    [[nodiscard]] static time_point now() noexcept
    {
        return Provider::nowUs();
    }

    template <typename Rep, typename Period>
    [[nodiscard]] static time_point add(time_point tp, const std::chrono::duration<Rep, Period>& d) noexcept
    {
        return tp + static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(d).count());
    }

    [[nodiscard]] static time_point add(time_point tp, duration delta) noexcept
    {
        return tp + delta;
    }

    [[nodiscard]] static bool isDue(time_point now, time_point expiry) noexcept
    {
        return now >= expiry;
    }
};

/// @brief Clock policy driven by a custom millisecond tick provider (e.g. millis(), HAL_GetTick()).
/// @tparam Provider Struct with static `uint32_t nowMs()` method.
template <typename Provider>
class MillisecondTickClockPolicy {
public:
    using time_point = uint32_t;
    using duration = uint32_t;

    [[nodiscard]] static time_point now() noexcept
    {
        return Provider::nowMs();
    }

    template <typename Rep, typename Period>
    [[nodiscard]] static time_point add(time_point tp, const std::chrono::duration<Rep, Period>& d) noexcept
    {
        return tp + static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
    }

    [[nodiscard]] static time_point add(time_point tp, duration delta) noexcept
    {
        return tp + delta;
    }

    [[nodiscard]] static bool isDue(time_point now, time_point expiry) noexcept
    {
        return now >= expiry;
    }
};

/// @brief Native ESP32 Hardware Timer clock policy using `esp_timer_get_time()` (64-bit microsecond counter).
class EspTimerClockPolicy {
public:
    using time_point = uint64_t;
    using duration = uint64_t;

    [[nodiscard]] static time_point now() noexcept
    {
#if defined(CORIUM_NATIVE_ESP_TIMER)
        int64_t t = esp_timer_get_time();
        return t > 0 ? static_cast<uint64_t>(t) : 0;
#else
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
#endif
    }

    template <typename Rep, typename Period>
    [[nodiscard]] static time_point add(time_point tp, const std::chrono::duration<Rep, Period>& d) noexcept
    {
        return tp + static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(d).count());
    }

    [[nodiscard]] static time_point add(time_point tp, duration delta) noexcept
    {
        return tp + delta;
    }

    [[nodiscard]] static bool isDue(time_point now, time_point expiry) noexcept
    {
        return now >= expiry;
    }
};

/// @brief FreeRTOS RTOS Tick clock policy using `xTaskGetTickCount()`.
class FreeRtosClockPolicy {
public:
    using time_point = uint32_t;
    using duration = uint32_t;

    [[nodiscard]] static time_point now() noexcept
    {
#if defined(CORIUM_NATIVE_FREERTOS)
        return static_cast<uint32_t>(xTaskGetTickCount());
#else
        return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
#endif
    }

    template <typename Rep, typename Period>
    [[nodiscard]] static time_point add(time_point tp, const std::chrono::duration<Rep, Period>& d) noexcept
    {
        return tp + static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
    }

    [[nodiscard]] static time_point add(time_point tp, duration delta) noexcept
    {
        return tp + delta;
    }

    [[nodiscard]] static bool isDue(time_point now, time_point expiry) noexcept
    {
        return now >= expiry;
    }
};

} // namespace corium
