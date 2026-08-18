/**
 * @file FreeRtos.hpp
 * @ingroup embedded
 * @brief FreeRTOS ISR event sink and hardware context-switching helpers.
 */

#pragma once

#include <utility>
#include "corium/policies/QueuePolicies.hpp"

#if defined(FREERTOS) || defined(INC_FREERTOS_H) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#define CORIUM_HAS_FREERTOS_INCLUDES 1
#else
// Mock definitions for host build and desktop testing
using BaseType_t = long;
#define pdTRUE 1
#define pdFALSE 0
#define portYIELD_FROM_ISR(x) ((void)(x))
#endif

namespace corium::embedded {

/// @ingroup embedded
/// @brief FreeRTOS-specialized ISR sink managing RTOS task context switches.
/// @tparam EventSinkType Target underlying event sink.
template <typename EventSinkType>
class FreeRtosIsrSink {
public:
    constexpr FreeRtosIsrSink() noexcept = default;

    constexpr explicit FreeRtosIsrSink(EventSinkType sink) noexcept
        : _sink(sink)
    {}

    /// @brief Post event from FreeRTOS ISR and track if context switch is needed.
    /// @tparam Event Concrete event type.
    /// @param event Event instance.
    /// @param pxHigherPriorityTaskWoken Pointer to FreeRTOS task unblocking flag.
    /// @param priority Event priority level.
    template <typename Event>
    void postFromIsr(Event&& event, BaseType_t* pxHigherPriorityTaskWoken = nullptr, EventPriority priority = EventPriority::Normal) noexcept
    {
        _sink.post(std::forward<Event>(event), priority);
        if (pxHigherPriorityTaskWoken) {
            *pxHigherPriorityTaskWoken = pdTRUE;
        }
    }

    /// @brief Post high priority event from FreeRTOS ISR and track context switch.
    template <typename Event>
    void postHighPriorityFromIsr(Event&& event, BaseType_t* pxHigherPriorityTaskWoken = nullptr) noexcept
    {
        _sink.postHighPriority(std::forward<Event>(event));
        if (pxHigherPriorityTaskWoken) {
            *pxHigherPriorityTaskWoken = pdTRUE;
        }
    }

    /// @brief Yield to higher priority unblocked task if flag was set.
    static void yieldFromIsr(BaseType_t xHigherPriorityTaskWoken) noexcept
    {
        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }

    /// @brief Access underlying sink handle.
    [[nodiscard]] constexpr EventSinkType& sink() noexcept { return _sink; }
    [[nodiscard]] constexpr const EventSinkType& sink() const noexcept { return _sink; }

private:
    EventSinkType _sink{};
};

/// @brief Helper function to construct FreeRtosIsrSink from an event sink handle.
template <typename EventSinkType>
[[nodiscard]] constexpr auto makeFreeRtosIsrSink(EventSinkType sink) noexcept
{
    return FreeRtosIsrSink<EventSinkType>(sink);
}

} // namespace corium::embedded
