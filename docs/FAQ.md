# Frequently Asked Questions (FAQ)

### 1. Is Corium truly zero-heap?
**Yes.** All event storage, timers, handlers (via Small Buffer Optimized `FastDelegate`), background service registrations, and coroutines allocate memory in static arrays and stack frames defined at compile time. This is verified automatically in CI via custom overloaded global `new`/`delete` hooks in `test_zero_heap_guarantee.cpp`.

---

### 2. Can I use Corium without C++20 coroutines?
**Yes.** The `corium/async/` module is entirely modular and optional. You can use standard CRTP static polymorphism (`corium::Application`) with synchronous event handlers or background worker services (`corium::BackgroundService`) without using coroutines.

---

### 3. How do I size the event queue capacity?
The queue capacity is a template parameter on `BoundedMpscQueuePolicy` or `RuntimeBuilder` (default `1024`). A good rule of thumb is:
$$\text{Capacity} \ge 2 \times (\text{Max Producer Event Burst Rate} \times \text{Max Dispatch Latency})$$
For bare-metal microcontrollers (ARM Cortex-M), you can downscale to 64 or 128 elements to minimize static memory footprint.

---

### 4. Is Corium safe to call from Hardware Interrupt Service Routines (ISRs)?
**Yes.** Use `corium::embedded::IsrEventSink` or `corium::embedded::FreeRtosIsrSink`. These handles push events into the lock-free Dmitry Vyukov ring buffer without acquiring mutexes or allocating memory, ensuring ISR safety across ARM CMSIS, ESP32, and STM32 platforms.

---

### 5. What happens when the event queue becomes full?
Corium provides configurable **Overflow Policies**:
- `DropNewestOverflowPolicy` (default): Drops the incoming event quietly.
- `DropOldestOverflowPolicy`: Evicts the oldest unhandled event to prioritize fresh data.
- `AuditOverflowPolicy`: Increments an atomic drop counter for diagnostics and monitoring.
- `PanicOverflowPolicy`: Asserts or invokes the panic handler on overflow.

---

### 6. Can I use Corium with FreeRTOS or Zephyr?
**Yes.** Corium includes:
- `corium::FreeRtosClockPolicy`: Drives the timer scheduler from `xTaskGetTickCount()`.
- `corium::embedded::FreeRtosIsrSink`: Manages `xHigherPriorityTaskWoken` and `portYIELD_FROM_ISR()`.
- `corium::embedded::InterruptLock`: Wraps `taskENTER_CRITICAL()` / `portENTER_CRITICAL()`.

---

### 7. How do I integrate Corium into my CMake project?
Corium is a header-only library with two distribution options:

**Option A: Add as submodule / subdirectory**
```cmake
add_subdirectory(corium)
target_link_libraries(my_app PRIVATE corium)
```

**Option B: Single-Header distribution**
```cpp
#include "corium/single_include/corium.hpp"
```
