# 🚀 Corium v1.0.0 — Production-Ready Release

Announcing the official **v1.0.0** release of **Corium**, a high-performance, zero-heap, header-only C++20 event-driven runtime engine designed for mission-critical, real-time, desktop, and embedded systems (ARM Cortex-M, ESP32, FreeRTOS).

---

## ✨ Key Highlights

- **🔒 Zero-Heap Guarantee**: Zero dynamic memory allocations (`malloc`/`free`/`new`/`delete`) in execution paths. All queues, delegates, telemetry buffers, and state machines are statically bounded and cache-friendly.
- **⚡ Dmitry Vyukov Lock-Free MPSC**: Cache-line aligned (`alignas(64)`) multi-producer single-consumer ring buffer eliminating false sharing.
- **🛡️ Two-Way RAII Lifecycle**: Complete construction and destruction order independence between `Application` and `Runtime`.
- **⏱️ C++20 Coroutines & Async**: Native `Task<T>`, `whenAll()`, `whenAny()`, `Generator<T>`, `delay()`, and `CancellationToken`.
- **🔄 Hierarchical FSM**: Declarative state machines with shallow history states, internal transitions, guards, and deterministic entry/exit actions.
- **📡 Lock-Free IPC**: High-throughput cross-process communication via Shared Memory and Unix Domain Sockets.
- **📊 Flight Recorder Profiler**: Nanosecond event tracing with direct export to Google Chrome Tracing and [Perfetto UI](https://ui.perfetto.dev).
- **🔌 Embedded & FreeRTOS**: Direct ISR event sink integration (`portYIELD_FROM_ISR`) and hardware interrupt locks (`InterruptLock`).
- **📦 Single-Header Distribution**: Standalone drop-in `corium.hpp` fully tested under strict `-fno-rtti -fno-exceptions`.

---

## 🧪 Quality & Verification

- **100% Test Pass Rate**: 90/90 unit and stress test suites.
- **Sanitizers**: Validated with **AddressSanitizer (ASan)**, **UndefinedBehaviorSanitizer (UBSan)**, and **ThreadSanitizer (TSan)**.
- **Code Coverage**: Line-by-line coverage verified on GCC 14 with atomic multi-thread counters.
- **Documentation**: 100% Doxygen API coverage with interactive Mermaid.js diagrams.

---

## 🚀 Quick Start with Single-Header

Download `corium.hpp` below and include it directly in your project:

```cpp
#include "corium.hpp"

struct PingEvent { int id; };
using MyEvents = std::variant<corium::QuitEvent, PingEvent>;

class MyApp : public corium::Application<MyApp, MyEvents> {
public:
    void onRegisterHandlers() {
        on([](const PingEvent& e) {
            // Process event at line-rate with zero allocations
        });
    }
};

int main() {
    MyApp app;
    corium::RuntimeBuilder::WithEvents<MyEvents>::Build runtime;
    runtime.initialize(app);
    
    runtime.eventSink().post(PingEvent{42});
    runtime.pump();
}
```

---

## 📖 Documentation & Guides

- 🌐 **Interactive Documentation**: https://simonecavalleri.github.io/corium/
- 🏛️ **Architecture Guide**: [docs/ARCHITECTURE.md](https://github.com/simoneCavalleri/corium/blob/main/docs/ARCHITECTURE.md)
- 🍳 **Cookbook & Patterns**: [docs/COOKBOOK.md](https://github.com/simoneCavalleri/corium/blob/main/docs/COOKBOOK.md)
- ❓ **Frequently Asked Questions**: [docs/FAQ.md](https://github.com/simoneCavalleri/corium/blob/main/docs/FAQ.md)
