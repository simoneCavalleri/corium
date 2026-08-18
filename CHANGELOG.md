# Changelog

All notable changes to **Corium** are documented in this file in accordance with [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) and [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.1.0] - 2026-08-18

### Added
- **Deterministic Record & Replay**:
  - `corium::wire::EventJournalWriter`: Circular in-memory event journal recorder with schema hash validation and per-record CRC-16 checksums.
  - `corium::wire::EventJournalReader`: Deterministic post-mortem event journal replay directly into any `EventSink`.
- **Hardware Bus ISR Adapters**:
  - `corium::embedded::SpiAdapter` / `SpiFrame`: Hardware SPI frame extraction and DMA/ISR event dispatching.
  - `corium::embedded::I2cAdapter` / `I2cTransaction`: Hardware I²C transaction frame extraction and DMA/ISR event dispatching.
  - `corium::embedded::CanAdapter` / `CanFdAdapter`: Native CAN 2.0B / CAN-FD frame parsing.
  - `corium::embedded::DmaUartBuffer`: Lock-free circular DMA buffer for high-throughput UART serial streams.
- **Low-Latency Network & Telemetry**:
  - `corium::net::StaticUdpChannel`: Zero-copy static UDP datagram channel with `WirePacket` framing, `sendEvent()`, and `receiveAndPush()`.
  - Master network umbrella header `corium/net/net.hpp`.
- **Advanced Coroutine Synchronization**:
  - `corium::async::Channel<T, Capacity>`: Bounded static async channel for coroutine producer-consumer message passing with compile-time backpressure suspension.
  - `corium::async::AsyncSemaphore`: Counting async semaphore for non-blocking coroutine concurrency throttling.
- **Observability & Prometheus Metrics**:
  - `corium::profiler::Counter`: Zero-heap 64-bit atomic monotonically increasing event counter.
  - `corium::profiler::Gauge`: Zero-heap 64-bit atomic instantaneous value metric.
  - `corium::profiler::Histogram<NumBuckets>`: Zero-heap static bucketing latency distribution tracker.
  - `corium::profiler::formatPrometheusCounter()` / `formatPrometheusGauge()`: Standard Prometheus text exposition formatters.
- **Static Topic-Based Event Fan-Out**:
  - `corium::EventRouter<EventVariant, MaxSubscribers, MaxTopics>`: Multi-subscriber publish/subscribe fan-out router with typed `subscribeEvent<Event>()` and `publishEvent()`.
- **FSM Guard Conditions**:
  - `corium::fsm::Transition<Source, Event, Target, Guard, Action>`: Compile-time predicate evaluation to conditionally allow or block state transitions.
- **Documentation & Recipes**:
  - Expanded `docs/COOKBOOK.md` to 16 comprehensive real-time recipes.
  - Updated `docs/ARCHITECTURE.md` with new layered Mermaid diagrams and bare-metal memory footprint model.
  - Updated Doxygen taxonomy `docs/groups.dox` with `@defgroup net`.

### Fixed
- **Bare-Metal ARM Cortex-M Startup**:
  - Added `PROVIDE ( __end__ = . );` symbol in `linker.ld` for Newlib semihosting heap/stack initializers (`librdimon.a`).
  - Added Cortex-M vector table `g_pfnVectors` and `Reset_Handler` startup routine.
  - Refactored `main()` to avoid taking function pointer address under ISO C++ `-Wpedantic`.
- **C++20 Coroutine Channel Awaiters**:
  - Corrected `PushAwaiter` and `PopAwaiter` execution logic to perform non-suspending push/pop inside `await_ready()`.

---

## [1.0.0] - 2026-08-18

### Added
- **Async Coroutine Combinators**:
  - `corium::async::whenAll()`: Non-blocking parallel task combinator returning `Task<std::tuple<T...>>` (value) or `Task<void>`.
  - `corium::async::whenAny()`: Fastest-wins task combinator returning indexed result structure.
  - `corium::async::CancellationToken`: Lock-free atomic cooperative cancellation token with `whenCancelled()` awaiter.
  - `corium::async::Generator<T>`: Pull-based zero-heap lazy sequence generator compatible with C++20 ranges and for-loops.
- **Finite State Machine (FSM) Evolution**:
  - `corium::fsm::InternalTransition`: In-place state action execution without `onExit()` / `onEnter()` overhead.
  - `corium::fsm::ActionList<Actions...>`: Sequential composite action execution for transition triggers.
  - `corium::fsm::ShallowHistory<DefaultState>`: Pseudostate remembering previous active state in composite groups.
- **Wire Protocol v2**:
  - Added strict `version` (1B) and `reserved` (1B) fields in `WireHeader` with version mismatch rejection in `isValid()`.
- **Structured JSON Logging**:
  - `corium::logging::sinks::JsonLogSink`: Zero-heap structured JSON Lines (NDJSON) telemetry sink with automatic string escaping.
- **Cross-Platform IPC**:
  - `corium::ipc::PlatformChannel`: Portable datagram channel alias.
- **Real-Time Profiler Toggle**:
  - Added `enable()`, `disable()`, and `isEnabled()` runtime toggles in `LatencyTracker` and `FlightRecorderProfiler`.
- **Quality & CI**:
  - GitHub Actions `code-coverage` job with `lcov` filtering.
  - Multi-producer stress test suite (`test_stress_mpsc.cpp`) under heavy thread contention (8 producers, 400k events).
  - Bit-flip and random-mutation fuzzer (`fuzz_wire_deserialize.cpp`) for binary wire deserialization.
  - Documentation coverage tool (`tools/doc_coverage.py`).
  - Architectural Cookbook (`docs/COOKBOOK.md`) and Migration Guide (`docs/MIGRATION.md`).

### Fixed
- Fixed `<array>` header inclusion in `test_fast_delegate.cpp` for MSVC compilation.
- Fixed `modernize-loop-convert` clang-tidy diagnostic in fast delegate tests.
- Fixed WatchdogService autonomous supervision retry loop for Windows scheduling clock resolution.
- Cleaned unused header chains and standardized C++20 designated initializers across codebase.

---

## [0.8.0] - 2026-08-16

### Initial Features
- Lock-free MPSC Vyukov ring buffer engine (`BasicEventBus`).
- CRTP static polymorphism application base (`Application<Derived>`).
- FastDelegate small buffer optimization (zero dynamic allocation).
- Zero-heap timer scheduler (`TimerScheduler`) and hardware clock policies (Chrono, Manual, Tick, ESP32, FreeRTOS).
- Automotive Watchdog Supervisor (`WatchdogSupervisor`) and Circuit Breaker (`CircuitBreaker`).
- Zero-copy shared memory IPC (`IpcChannel`, `UdsChannel`, `ShmMpscQueue`).
- Circular in-memory flight recorder (`FlightRecorder`) exporting Chrome Tracing JSON.
- Single-header amalgamator tool (`tools/amalgamate.py`).
- 5 comprehensive industrial showcase samples (Smart Grid, Aerospace UAV, HFT Market Data, Automotive Braking ECU, Drone Ground Control).
