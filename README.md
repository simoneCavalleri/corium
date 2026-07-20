# Corium

**Corium** is a high-performance, header-only, zero-allocation C++20 application runtime designed for event-driven systems, games, GUI frameworks, embedded software, and real-time applications.

---

## 🌟 Key Features

- **Header-Only C++20 Library**: Simply include `#include <corium/corium.hpp>` and add `target_include_directories(app PRIVATE include)`. No static or shared library binaries required.
- **Auto-Deduced Event Handlers**: Register handlers via `on([](const MyEvent& e) { ... })` with zero template argument boilerplate.
- **Power-Saving `waitAndPump()`**: Block efficiently until events arrive or timeout expires without custom condition variable boilerplate.
- **Zero-Allocation Hot Path**: Hot event dispatching operates with 0 dynamic heap allocations using a lock-free **MPSC RingBuffer** (Vyukov algorithm) and **Small Buffer Optimization (SBO)** delegates.
- **Policy-Based Runtime Design**: Modular compile-time policies for queueing, signaling, and dispatching:
  - `QueuePolicy`: Bounded Lock-Free MPSC, Traditional Blocking Queue.
  - `SignalPolicy`: Edge-triggered Callback, Futex C++20 `std::atomic::wait()`, Busy-spin / Polling (sub-microsecond latency), Linux `eventfd` / `epoll`.
  - `DispatchPolicy`: O(1) static array indexing with zero type erasure.
- **Fluent Compile-Time `RuntimeBuilder`**: Configure custom runtimes cleanly via compile-time builder types.
- **Compile-Time Customizable `EventVariant`**: Use default event types out of the box or supply your own custom `std::variant<MyEvents...>` type list.
- **Zero Type Erasure**: Replaces `std::function` and hash maps with `EventHandlerDelegate` (32-byte inline storage) and direct array indexing by `event.index()`.
- **Background Service Integration**: Thread-safe `BackgroundService` execution via `std::jthread` and `std::stop_token` graceful cancellation.

---

## 🚀 Quick Start

### Minimal Example

```cpp
#include <corium/corium.hpp>
#include <chrono>
#include <iostream>

using namespace corium;

class DemoApp : public AppCore {
protected:
    void onRegisterHandlers() override {
        // Auto-deduces TickEvent from lambda argument signature
        on([](const TickEvent& event) {
            std::cout << "Tick received! Elapsed: " << event.deltaTime << "s\n";
        });
    }

    void onInitialize() override {
        std::cout << "DemoApp initialized.\n";
    }
};

int main() {
    Runtime runtime;
    DemoApp app;

    runtime.initialize(app);
    
    // External event loop waiting for events and pumping efficiently
    while (!runtime.quitRequested()) {
        runtime.waitAndPump(std::chrono::milliseconds(50));
    }

    runtime.shutdown();
    return 0;
}
```

---

## ⚙️ Policy-Based Runtime Design & `RuntimeBuilder`

Corium allows developers to customize queueing, signaling, and event dispatching at compile time:

```cpp
#include <corium/corium.hpp>

using namespace corium;

// 1. Standard Default Runtime
using StandardRuntime = RuntimeBuilder<>::Build;

// 2. Embedded / Real-time Polling Runtime (sub-microsecond latency, 4096 capacity)
using RealtimeRuntime = RuntimeBuilder<>
    ::WithCapacity<4096>
    ::WithSignalPolicy<NoSignalPolicy>
    ::Build;

// 3. C++20 Futex (std::atomic::wait) Power-Saving Runtime
using FutexRuntime = RuntimeBuilder<>
    ::WithSignalPolicy<AtomicWaitSignalPolicy>
    ::Build;

// 4. Custom Event Variant
struct MyCustomEvent { std::string data; };
using MyEvents = std::variant<QuitEvent, MyCustomEvent>;

using CustomAppRuntime = RuntimeBuilder<>
    ::WithEvents<MyEvents>
    ::WithSignalPolicy<AtomicWaitSignalPolicy>
    ::Build;
```

---

## 📊 Benchmarks

Corium includes an automated benchmarking suite built with **Google Benchmark** to measure dispatch latency, MPSC ring buffer throughput, and signal policy efficiency:

```bash
# Configure and build benchmarks
cmake -B build -DCORIUM_BUILD_BENCHMARKS=ON
cmake --build build

# Run benchmarks
./build/corium_benchmarks
```

---

## 🛠️ Integration & CMake

Corium is a header-only library target using CMake `INTERFACE`:

```cmake
cmake_minimum_required(VERSION 3.10)
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
