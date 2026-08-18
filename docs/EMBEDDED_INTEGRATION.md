# Corium Embedded Integration Guide

This guide details how to integrate **Corium** into embedded toolchains, real-time operating systems (RTOS), and bare-metal IDEs with zero dynamic memory overhead.

---

## 1. Embedded Guarantees & Prerequisites

* **C++ Standard:** C++20 (`-std=c++20` or `-std=gnu++20`).
* **Dynamic Heap Allocations:** `0 bytes` (no `malloc`, `free`, or `new`).
* **Vtables & RTTI:** `0 bytes` (compiles cleanly with `-fno-rtti` and `-fno-exceptions`).
* **Typical Memory Footprint (ARM Cortex-M):**
  * **Flash (.text):** ~3 to 6 KB.
  * **SRAM (.data + .bss):** < 1 to 2 KB.

---

## 2. STM32CubeIDE / Keil MDK / IAR Embedded Workbench

For IDEs utilizing GCC ARM, Arm Compiler 6 (armclang), or IAR EWARM:

### Step 1: Copy Single-Header Distribution
Copy `single_include/corium.hpp` into your project's include folder (e.g. `Core/Inc/` or `Middlewares/Third_Party/corium/`).

### Step 2: Configure Compiler Flags
In your IDE's C/C++ Build Settings:

* **Language Dialect:** `C++20` (`-std=c++20` or `-std=gnu++20`)
* **Optimization:** Size (`-Os`) or Speed (`-O2`)
* **Code Generation:**
  * Disable RTTI: `-fno-rtti`
  * Disable Exceptions: `-fno-exceptions`
  * Enable Section Garbage Collection: `-ffunction-sections -fdata-sections -Wl,--gc-sections`

### Step 3: Usage in `main.cpp`
```cpp
#include "corium.hpp"

struct ButtonPressEvent { uint16_t pin; };
using MyEvents = std::variant<corium::QuitEvent, ButtonPressEvent>;

using AppRuntime = corium::RuntimeBuilder<MyEvents>
    ::WithCapacity<16>
    ::WithStoragePolicy<corium::CompactStoragePolicy>
    ::Build;

AppRuntime runtime;

extern "C" void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    runtime.isrSink().postFromIsr(ButtonPressEvent{GPIO_Pin}, corium::EventPriority::High);
}

extern "C" void app_main() {
    while (1) {
        runtime.pump();
    }
}
```

---

## 3. ESP-IDF (ESP32, ESP32-S3, ESP32-C3, ESP32-C6)

### Option A: As an Extra Component
Clone or submodule Corium into `your_project/components/corium/` with a minimal `CMakeLists.txt`:
```cmake
idf_component_register(
    INCLUDE_DIRS "include"
)
```

### Option B: Using ESP Component Registry (IDF Component Manager)
Add to your `main/idf_component.yml`:
```yaml
dependencies:
  corium:
    version: "^1.1.0"
```

### Project Configuration (`sdkconfig`)
Ensure C++20 and exception settings are configured in `menuconfig` or `sdkconfig.defaults`:
```ini
CONFIG_COMPILER_CXX_STANDARD_20=y
CONFIG_COMPILER_CXX_EXCEPTIONS_OFF=y
CONFIG_COMPILER_CXX_RTTI_OFF=y
```

### FreeRTOS & Hardware Timer Integration
```cpp
#include <corium/corium.hpp>
#include <corium/timers/ClockPolicies.hpp>

using Esp32Runtime = corium::RuntimeBuilder<MyEvents>
    ::WithClockPolicy<corium::EspTimerClockPolicy>
    ::Build;
```

---

## 4. PlatformIO (`platformio.ini`)

For PlatformIO projects targeting STM32, ESP32, RP2040, or Teensy:

```ini
[env:nucleo_f401re]
platform = ststm32
board = nucleo_f401re
framework = stm32cube

lib_deps =
    https://github.com/simoneCavalleri/corium.git

build_flags =
    -std=gnu++20
    -fno-rtti
    -fno-exceptions
    -ffunction-sections
    -fdata-sections
    -Wl,--gc-sections
```

---

## 5. Raspberry Pi Pico SDK (RP2040 / RP2350)

In your project's `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

project(pico_corium_app C CXX ASM)
pico_sdk_init()

include(FetchContent)
FetchContent_Declare(
    corium
    GIT_REPOSITORY https://github.com/simoneCavalleri/corium.git
    GIT_TAG main
)
FetchContent_MakeAvailable(corium)

add_executable(pico_app main.cpp)
target_link_libraries(pico_app pico_stdlib corium::corium)

target_compile_options(pico_app PRIVATE
    -std=c++20
    -fno-rtti
    -fno-exceptions
)
```

---

## 6. Zephyr RTOS

In your Zephyr application folder:

### 1. `CMakeLists.txt`
```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_corium_app)

include(FetchContent)
FetchContent_Declare(corium GIT_REPOSITORY https://github.com/simoneCavalleri/corium.git GIT_TAG main)
FetchContent_MakeAvailable(corium)

target_sources(app PRIVATE src/main.cpp)
target_link_libraries(app PRIVATE corium::corium)
```

### 2. `prj.conf`
```ini
CONFIG_CPP=y
CONFIG_STD_CPP20=y
CONFIG_EXCEPTIONS_OFF=y
CONFIG_RTTI_OFF=y
```

---

## 7. Memory Tuning for Ultra-Constrained MCUs (< 32KB Flash)

When targeting ultra-small microcontrollers (e.g. STM32C0, ATtiny, Cortex-M0+):

1. **Use `CompactStoragePolicy`**: Reduces FastDelegate SBO inline size from 32 bytes to 16 bytes.
2. **Dimension Queue Capacity Statically**: Set `WithCapacity<8>` or `WithCapacity<16>` to save SRAM.
3. **Limit Max Timers**: Set `WithMaxTimers<4>` to minimize min-heap memory.
4. **Compile with `-Os`**: Instructs the compiler to favor size-optimized instructions.
