# 🚀 Corium v1.1.0 — Embedded Bus Adapters, Telemetry & Coroutine Channels

Announcing the official **v1.1.0** release of **Corium**, the high-performance, zero-heap, header-only C++20 event-driven runtime engine for mission-critical, real-time desktop, and embedded systems (ARM Cortex-M, ESP32, FreeRTOS).

---

## ✨ What's New in v1.1.0

### 1. 📼 Deterministic Record & Replay (`EventJournal`)
* `corium::wire::EventJournalWriter`: Circular in-memory event journal recorder with schema hash validation and per-record CRC-16 checksums.
* `corium::wire::EventJournalReader`: Deterministic post-mortem event replay directly into any `EventSink` for black-box debugging.

### 2. 🔌 Embedded Bus Adapters & Hardware ISR Handles
* `corium::embedded::SpiAdapter`: Hardware SPI frame extraction and DMA/ISR event dispatching.
* `corium::embedded::I2cAdapter`: Hardware I²C transaction frame extraction and DMA/ISR event dispatching.
* `corium::embedded::CanAdapter` / `CanFdAdapter`: CAN 2.0B / CAN-FD frame parsing and ISR posting.
* `corium::embedded::DmaUartBuffer`: Lock-free circular DMA buffer for high-throughput UART serial streams.

### 3. 🌐 Low-Latency Network & Telemetry (`StaticUdpChannel`)
* `corium::net::StaticUdpChannel`: Zero-heap static UDP datagram channel with `WirePacket` framing, `sendEvent()`, and `receiveAndPush()`.
* Umbrella header `corium/net/net.hpp`.

### 4. 🔀 Advanced Coroutine Synchronization
* `corium::async::Channel<T, Capacity>`: Bounded static async channel for coroutine producer-consumer message passing with compile-time backpressure suspension.
* `corium::async::AsyncSemaphore`: Counting async semaphore for non-blocking coroutine concurrency throttling.

### 5. 📊 Observability & Prometheus Metrics Exporter
* `corium::profiler::Counter`: Zero-heap 64-bit atomic monotonically increasing event counter.
* `corium::profiler::Gauge`: Zero-heap 64-bit atomic instantaneous value metric.
* `corium::profiler::Histogram<NumBuckets>`: Zero-heap static bucketing latency distribution tracker.
* `formatPrometheusCounter()` / `formatPrometheusGauge()`: Standard Prometheus exposition text formatters.

### 6. 📢 Static Topic-Based Event Fan-Out (`EventRouter`)
* `corium::EventRouter<EventVariant, MaxSubscribers, MaxTopics>`: Multi-subscriber publish/subscribe fan-out router with typed `subscribeEvent<Event>()` and `publishEvent()`.

### 7. 🛡️ Guarded FSM State Transitions
* `corium::fsm::Transition<Source, Event, Target, Guard, Action>`: Compile-time predicate evaluation to conditionally allow or block state transitions.

### 8. 📦 Ecosystem Manifests & Integration Guides
* [library.json](library.json) manifest for **PlatformIO**.
* [idf_component.yml](idf_component.yml) manifest for **ESP-IDF Component Registry**.
* [docs/EMBEDDED_INTEGRATION.md](docs/EMBEDDED_INTEGRATION.md) for STM32CubeIDE, ESP-IDF, Keil MDK, IAR, Raspberry Pi Pico SDK, and Zephyr RTOS.
* Expanded [docs/COOKBOOK.md](docs/COOKBOOK.md) to **16 production-grade recipes**.

---

## 🧪 Verification & Quality Gates

* **119 / 119 Unit & Integration Tests Passed (100%)**.
* **Zero Dynamic Heap Allocations** on hot path (`corium_zero_heap_test`).
* **100% Doxygen Documentation Coverage** across all 84 headers (`tools/doc_coverage.py --strict`).
* **Multi-Platform CI**: Linux (GCC 13/14, Clang 18), macOS (AppleClang), Windows (MSVC 2022), Bare-Metal ARM QEMU (Cortex-M3, Cortex-M4, Cortex-M7).

---

## 🚀 Quick Start with Single-Header

Download `corium.hpp` and include it directly:

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
