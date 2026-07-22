# Corium

**Corium** is a high-performance, header-only, **zero-heap allocation, zero-RTTI** C++20 application framework designed for **Multi-Producer Single-Consumer (MPSC)** event-driven systems.

Whether running on **bare-metal microcontrollers, embedded RTOS, or high-performance desktop systems** (Linux, Windows, macOS), Corium guarantees **zero dynamic memory allocations** on the heap, **zero virtual table / RTTI overhead**, and **pure compile-time static dispatching**.

---

## 🌟 Key Features

- **Zero-Heap Allocation Guaranteed**: Hot event enqueueing and dispatching operate with **0 dynamic heap allocations** using a lock-free **MPSC RingBuffer** (Vyukov algorithm) and static stack/inline storage.
- **Zero RTTI & Zero Vtables**: Built for clean compilation with `-fno-rtti` and `-fno-exceptions`. Replaces virtual methods and type erasure with **CRTP static polymorphism** and compile-time template metaprogramming.
- **Multi-Producer Single-Consumer (MPSC)**: Multiple thread/hardware event producers push concurrently into a lock-free ring buffer, while a single consumer thread processes and dispatches events efficiently.
- **CRTP Static Application Core (`AppCoreT`)**: Define application logic without virtual methods (`override`). Lifecycle hooks (`onConfigureServices`, `onRegisterHandlers`, `onInitialize`, `onShutdown`) are resolved statically at compile time.
- **Multi-Threaded Background Services**: Define background worker loops using `std::jthread` and `std::stop_token`. Managed automatically via `onConfigureServices(ServiceRegistry& registry)` with zero heap allocation.
- **Header-Only C++20 Framework**: Include `#include <corium/corium.hpp>` and link with CMake `INTERFACE`. No pre-compiled binaries required.
- **Auto-Deduced Event Handlers**: Register handlers via `on([](const MyEvent& e) { ... })` with zero template argument boilerplate.
- **Policy-Based Modular Runtime**:
  - `QueuePolicy`: Bounded Lock-Free MPSC Queue (`BoundedMpscQueuePolicy`).
  - `SignalPolicy`: Busy-Spin Polling (`NoSignalPolicy`), Edge-Triggered Callback (`CallbackSignalPolicy`), Futex C++20 `std::atomic::wait()` (`AtomicWaitSignalPolicy`), Linux `eventfd` (`EventFdSignalPolicy`).
  - `StoragePolicy`: Compile-time handler capacity & delegate inline storage configuration (`FixedStoragePolicy`, `DefaultStoragePolicy`, `CompactStoragePolicy`, `LargeStoragePolicy`).
- **Fluent `RuntimeBuilder`**: Configure custom runtimes cleanly via compile-time builder types.

---

## 🚀 Quick Start

### Minimal Application Example (CRTP & Zero-Heap)

```cpp
#include <corium/corium.hpp>
#include <iostream>

using namespace corium;

// Application uses CRTP static inheritance
class DemoApp : public AppCoreT<DemoApp, Runtime::EventBusType> {
public:
    void onRegisterHandlers() {
        // Auto-deduces UpdateEvent from lambda argument signature
        on([this](const UpdateEvent& event) {
            _frameCount++;
            std::cout << "Frame #" << _frameCount << " (dt: " << event.deltaTime << "s)\n";

            if (_frameCount >= 5) {
                requestQuit();
            }
        });
    }

    void onInitialize() {
        std::cout << "DemoApp initialized.\n";
    }

    void onShutdown() {
        std::cout << "DemoApp shutdown complete.\n";
    }

private:
    int _frameCount = 0;
};

int main() {
    Runtime runtime;
    DemoApp app;

    runtime.initialize(app);

    // Event pump loop
    while (!runtime.quitRequested()) {
        runtime.eventSink().post(UpdateEvent{0.016}); // ~60 FPS dt
        runtime.pump();
    }

    runtime.shutdown();
    return 0;
}
```

---

## 🧵 Multi-Threaded Background Services & `ServiceRegistry`

Background services run on dedicated C++20 `std::jthread` worker loops, posting events concurrently to the lock-free MPSC ring buffer:

```cpp
#include <corium/corium.hpp>
#include <chrono>
#include <iostream>
#include <thread>

using namespace corium;

// 1. Background Worker Service (runs on its own std::jthread)
class SensorService : public BackgroundService<> {
public:
    void run(std::stop_token stopToken) {
        double elapsed = 0.0;
        while (!stopToken.stop_requested()) {
            postEvent(TickEvent{elapsed});
            elapsed += 0.2;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
};

// 2. Application Registering Background Service
class MultiThreadApp : public AppCoreT<MultiThreadApp, Runtime::EventBusType> {
public:
    SensorService sensorService;

    void onConfigureServices(ServiceRegistry& registry) {
        registry.registerService(sensorService);
    }

    void onRegisterHandlers() {
        on([](const TickEvent& e) {
            std::cout << "Sensor Tick received (time: " << e.deltaTime << "s)\n";
        });
    }
};

int main() {
    Runtime runtime;
    MultiThreadApp app;

    // Runtime automatically launches all service jthreads registered in onConfigureServices
    runtime.initialize(app);

    while (!runtime.quitRequested()) {
        runtime.waitAndPump(std::chrono::milliseconds(50));
    }

    // Runtime signals stop_token and cleanly joins all background service jthreads
    runtime.shutdown();
    return 0;
}
```

---

## ⚙️ Policy-Based Runtime Design & `RuntimeBuilder`

Corium allows developers to customize queueing, signaling, and storage policies at compile time:

```cpp
#include <corium/corium.hpp>

using namespace corium;

// 1. Standard Default Runtime (NoSignalPolicy, DefaultStoragePolicy: 8 handlers, 32B inline)
using StandardRuntime = RuntimeBuilder<>::Build;

// 2. High-Capacity Storage Policy Runtime (16 handlers per event, 64B inline storage)
using LargeRuntime = RuntimeBuilder<>
    ::WithStoragePolicy<LargeStoragePolicy>
    ::Build;

// 3. Custom Event Variant List
struct SensorReadEvent { float value; };
struct NetworkPacketEvent { uint16_t id; };

using MyEvents = std::variant<QuitEvent, SensorReadEvent, NetworkPacketEvent>;

using CustomAppRuntime = RuntimeBuilder<>
    ::WithEvents<MyEvents>
    ::WithCapacity<4096>
    ::WithSignalPolicy<NoSignalPolicy>
    ::WithStoragePolicy<FixedStoragePolicy<16, 64>>
    ::Build;
```

---

## 🧪 Unit Testing

Corium features a comprehensive unit test suite powered by **GoogleTest** and **CTest**:

```bash
# Configure and build unit tests
cmake -B build -DCORIUM_BUILD_TESTS=ON
cmake --build build

# Run unit tests via CTest or test executable
ctest --test-dir build --output-on-failure
# or
./build/corium_tests
```

### Strict Bare-Metal / Desktop Audit (`-fno-rtti -fno-exceptions`)

You can compile Corium applications with strict bare-metal flags to verify zero RTTI / zero exception dependency:

```bash
g++ -std=c++20 -fno-rtti -fno-exceptions -Iinclude samples/01_basic_app/main.cpp -o my_app
./my_app
```

---

## 🛠️ Integration & CMake

Corium is a header-only library target using CMake `INTERFACE`:

```cmake
cmake_minimum_required(VERSION 3.14)
project(MyProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add Corium header-only library
add_subdirectory(path/to/corium)

# Link your executable
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE corium)
```

---

## 📄 License

Corium is open-source software distributed under the MIT License.
