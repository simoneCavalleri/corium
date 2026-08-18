# Corium Cookbook — Real-Time Design Patterns

This guide provides tested, production-grade design patterns using **Corium**'s zero-heap C++20 architecture.

---

## 1. Request-Response via Coroutine Event Pair

In event-driven architectures, request-response interactions avoid blocking calls by pairing request and reply events through an asynchronous task.

```cpp
#include <corium/corium.hpp>
#include <corium/async/Task.hpp>
#include <corium/async/Delay.hpp>

// 1. Define Request & Response Events
struct DataRequestEvent {
    uint32_t requestId;
    const char* queryKey;
};

struct DataResponseEvent {
    uint32_t requestId;
    int32_t resultValue;
};

using AppEvents = std::variant<
    corium::QuitEvent,
    DataRequestEvent,
    DataResponseEvent
>;

// 2. Coroutine Worker performing non-blocking async query
corium::async::Task<int32_t> performQuery(corium::EventSinkT<AppEvents> sink, uint32_t reqId) {
    // Post request event into the lock-free event bus
    sink.post(DataRequestEvent{.requestId = reqId, .queryKey = "SENSOR_ALPHA"});

    // Yield execution back to event loop
    co_await corium::async::yield();

    // In a real system, the response handler resolves the result
    co_return 42;
}
```

---

## 2. Multi-Task Coordination with `whenAll` and `whenAny`

Coordinate parallel operations deterministically without dynamic allocations.

```cpp
#include <corium/corium.hpp>
#include <corium/async/WhenAll.hpp>
#include <corium/async/WhenAny.hpp>

corium::async::Task<float> readPressureSensor() {
    co_return 1013.25f;
}

corium::async::Task<float> readTemperatureSensor() {
    co_return 21.5f;
}

corium::async::Task<void> sampleEnvironment() {
    // Wait for both sensor readings simultaneously
    auto [pressure, temperature] = co_await corium::async::whenAll(
        readPressureSensor(),
        readTemperatureSensor()
    );

    std::cout << "Pressure: " << pressure << " hPa, Temp: " << temperature << " C\n";
}
```

---

## 3. Finite State Machine with Internal Transitions and Action Lists

Execute state actions without leaving the active state or incurring exit/entry overhead.

```cpp
#include <corium/fsm/fsm.hpp>

struct StateArmed { int targetVelocity{0}; };
struct StateDisarmed {};

struct ThrottleEvent { int demand; };
struct DisarmEvent {};

struct UpdateVelocityAction {
    void operator()(StateArmed& s, const ThrottleEvent& e) const {
        s.targetVelocity = e.demand;
    }
};

using DroneTable = corium::fsm::TransitionTable<
    // Internal transition: updates velocity in-place without triggering onExit / onEnter
    corium::fsm::InternalTransition<StateArmed, ThrottleEvent, corium::fsm::Always, UpdateVelocityAction>,
    // External transition: moves to Disarmed
    corium::fsm::Transition<StateArmed, DisarmEvent, StateDisarmed>
>;

corium::fsm::StateMachine<DroneTable, StateDisarmed, StateArmed> fsm;
```

---

## 4. Structured JSON Logging for Observability

Stream high-frequency logs to JSON Lines (NDJSON) with zero allocations.

```cpp
#include <corium/logging/logging.hpp>
#include <fstream>

std::ofstream logFile("application.log.json");
corium::logging::sinks::JsonLogSink jsonSink(logFile);

corium::logging::LogEventT<128> event;
event.timestampNs = 1700000000000000ULL;
event.level = corium::logging::LogLevel::Info;
event.category = "SAFETY_SUPERVISOR";
event.setMessage("Hardware watchdog refreshed successfully.");

jsonSink.write(event);
// Output: {"timestamp_ns":1700000000000000,"level":"INFO","category":"SAFETY_SUPERVISOR","message":"Hardware watchdog refreshed successfully."}
```

---

## 5. Cross-Platform Zero-Copy Shared Memory IPC

Share structured telemetry across separate OS processes without serialization overhead.

```cpp
#include <corium/ipc/ipc.hpp>

struct NavTelemetry {
    double latitude;
    double longitude;
    float altitude;
};

using FlightIpcEvents = std::variant<corium::QuitEvent, NavTelemetry>;

// Process A: Producer daemon
corium::ipc::IpcChannel<FlightIpcEvents, 256> producerChannel;
producerChannel.create("/corium_flight_shm");
producerChannel.post(NavTelemetry{.latitude = 45.4642, .longitude = 9.1900, .altitude = 150.0f});

// Process B: Consumer app
corium::ipc::IpcChannel<FlightIpcEvents, 256> consumerChannel;
consumerChannel.open("/corium_flight_shm");
FlightIpcEvents received;
if (consumerChannel.tryPop(received)) {
    // Process received zero-copy event
}
```

---

## 6. Deterministic Periodic Sampling with Manual Clock Simulation

Parameterize timers with `ManualClockPolicy` to step time deterministically in unit tests without wall-clock sleep delays.

```cpp
#include <corium/corium.hpp>
#include <corium/timers/ClockPolicies.hpp>

struct SampleTickEvent {};
using SensorEvents = std::variant<corium::QuitEvent, SampleTickEvent>;

using SimulatedRuntime = corium::RuntimeBuilder<SensorEvents>
    ::WithClockPolicy<corium::ManualClockPolicy>
    ::Build;

SimulatedRuntime runtime;
int sampleCount = 0;
runtime.reactor().template registerHandler<SampleTickEvent>([&](const SampleTickEvent&) {
    ++sampleCount;
});

// Schedule recurring timer every 100ms
runtime.timerScheduler().template postPeriodic<SampleTickEvent>(
    std::chrono::milliseconds(100),
    SampleTickEvent{}
);

// Fast-forward simulated clock by 350ms without waiting:
runtime.clockPolicy().advance(std::chrono::milliseconds(350));
runtime.pump(); // Exactly 3 ticks dispatched deterministically!
assert(sampleCount == 3);
```

---

## 7. Fault Isolation with Active Circuit Breaker

Protect critical systems against cascading failures using a zero-heap lock-free Circuit Breaker.

```cpp
#include <corium/safety/CircuitBreaker.hpp>

corium::safety::CircuitBreaker breaker(
    /* failureThreshold = */ 3,
    /* cooldownPeriodNs = */ 500'000'000ULL // 500ms
);

void handleRemoteRpc() {
    if (!breaker.allowExecution()) {
        // Fallback: Degraded local mode without blocking
        return;
    }

    bool success = executeHardwareI2cRead();
    if (success) {
        breaker.recordSuccess();
    } else {
        breaker.recordFailure(); // Trips to Open after 3 consecutive failures
    }
}
```

---

## 8. Telemetry Recording & Chrome Tracing JSON Export

Profile execution latencies in-memory and export full timeline traces for visualization in Perfetto or Google Chrome (`chrome://tracing`).

```cpp
#include <corium/corium.hpp>
#include <corium/profiler/ProfilerPolicies.hpp>
#include <fstream>

using ProfiledRuntime = corium::RuntimeBuilder<AppEvents>
    ::WithProfiler<corium::profiler::FlightRecorderProfiler<1024>>
    ::Build;

ProfiledRuntime runtime;
// Run application workload...

// Export traces directly to Chrome Tracing JSON file:
std::ofstream traceFile("benchmark_trace.json");
runtime.profiler().exportChromeTracingJson(traceFile);
```

---

## 9. Deterministic Event Journaling & Post-Mortem Replay

Record binary event streams with CRC-16 validation and schema hashing for deterministic black-box post-mortem replay.

```cpp
#include <corium/corium.hpp>
#include <corium/wire/EventJournal.hpp>
#include <array>

struct SensorSample { uint32_t sensorId; float value; };
using FlightEvents = std::variant<corium::QuitEvent, SensorSample>;

// 1. Record events into static memory buffer (e.g. battery-backed SRAM / Flash)
std::array<std::byte, 4096> journalStorage{};
corium::wire::EventJournalWriter<FlightEvents> writer(journalStorage);

writer.record(SensorSample{.sensorId = 1, .value = 101.3f});
writer.record(SensorSample{.sensorId = 2, .value = 24.5f});

// 2. Replay recorded journal deterministically into a live runtime EventSink
corium::BasicRuntime<FlightEvents> runtime;
corium::wire::EventJournalReader<FlightEvents> reader(journalStorage);

size_t replayed = reader.replayInto(runtime.eventSink());
runtime.pump(); // Dispatches all 2 recorded events with exact payload integrity!
```

---

## 10. Hardware SPI & I2C Sensor Ingestion from ISRs

Ingest high-rate sensor transactions from hardware DMA interrupts into Corium's event bus with zero heap allocations.

```cpp
#include <corium/corium.hpp>
#include <corium/embedded/SpiAdapter.hpp>
#include <corium/embedded/I2cAdapter.hpp>

struct ImuSampleEvent { int16_t accelX, accelY, accelZ; };
using EcuEvents = std::variant<corium::QuitEvent, ImuSampleEvent>;

corium::BasicRuntime<EcuEvents> runtime;

// Ingest from SPI DMA completion ISR:
void SPI1_DMA_IRQHandler() {
    corium::embedded::SpiFrame<6> rawFrame{};
    // Read 6 bytes of accelerometer registers from SPI Rx DMA buffer...
    rawFrame.data = {0x01, 0x00, 0x02, 0x00, 0x03, 0x00};

    auto imuEvent = ImuSampleEvent{
        .accelX = static_cast<int16_t>(rawFrame.data[0] | (rawFrame.data[1] << 8)),
        .accelY = static_cast<int16_t>(rawFrame.data[2] | (rawFrame.data[3] << 8)),
        .accelZ = static_cast<int16_t>(rawFrame.data[4] | (rawFrame.data[5] << 8))
    };

    runtime.isrSink().postFromIsr(imuEvent, corium::EventPriority::High);
}
```

---

## 11. Low-Latency Zero-Copy UDP Telemetry Streaming

Send and receive framed event datagrams over Ethernet/Wi-Fi without dynamic memory allocations.

```cpp
#include <corium/corium.hpp>
#include <corium/net/StaticUdpChannel.hpp>

struct DroneTelemetry { float altitude; float batteryVoltage; };
using DroneEvents = std::variant<corium::QuitEvent, DroneTelemetry>;

// Sender node (e.g. Ground Control Station):
corium::net::StaticUdpChannel<DroneEvents> udpSender;
udpSender.open();
udpSender.sendEvent("192.168.1.50", 9000, DroneTelemetry{.altitude = 120.5f, .batteryVoltage = 15.8f});

// Receiver node (e.g. On-Board Companion Computer):
corium::BasicRuntime<DroneEvents> runtime;
corium::net::StaticUdpChannel<DroneEvents> udpReceiver;
udpReceiver.bind(9000);

// In main event loop: receive and push directly into runtime sink
udpReceiver.receiveAndPush(runtime.eventSink());
runtime.pump();
```

---

## 12. Bounded Producer-Consumer Pipeline with Async Channel & Backpressure

Pass typed messages between asynchronous C++20 coroutines with compile-time backpressure.

```cpp
#include <corium/corium.hpp>
#include <corium/async/Channel.hpp>

// Create a static bounded channel with capacity of 8 items
corium::async::Channel<int, 8> dataChannel;

corium::async::Task<void> producer() {
    for (int i = 1; i <= 10; ++i) {
        // Suspends automatically if channel is full (backpressure)
        co_await dataChannel.push(i * 100);
    }
    dataChannel.close();
}

corium::async::Task<void> consumer() {
    while (true) {
        // Suspends automatically if channel is empty
        auto val = co_await dataChannel.pop();
        if (!val.has_value()) {
            break; // Channel closed and drained
        }
        std::cout << "Received: " << *val << "\n";
    }
}
```

---

## 13. Concurrency Throttling with Async Counting Semaphore

Limit the number of concurrent asynchronous operations without thread blocking.

```cpp
#include <corium/corium.hpp>
#include <corium/async/Semaphore.hpp>

// Allow maximum 2 concurrent flash write operations
corium::async::AsyncSemaphore flashWriteSemaphore(2);

corium::async::Task<void> flashWriter(int workerId) {
    co_await flashWriteSemaphore.acquire(); // Suspend until permit available

    std::cout << "Worker " << workerId << " writing to Flash...\n";
    co_await corium::async::yield();

    flashWriteSemaphore.release(); // Return permit to other waiting coroutines
}
```

---

## 14. Zero-Heap Prometheus Metrics Exporter

Instrument real-time embedded applications with atomic counters, gauges, and histograms, exporting directly to Prometheus format.

```cpp
#include <corium/corium.hpp>
#include <corium/profiler/Metrics.hpp>
#include <array>
#include <iostream>

// Define static zero-heap metrics
corium::profiler::Counter rxPackets("rx_packets_total", "Total network packets received");
corium::profiler::Gauge activeSessions("active_sessions", "Active client connections");

void onPacketReceived() {
    rxPackets.increment();
    activeSessions.set(5);
}

void handleMetricsHttpEndpoint() {
    std::array<char, 512> buffer{};
    size_t len = corium::profiler::formatPrometheusCounter(rxPackets, buffer);
    std::cout.write(buffer.data(), len);
    // Output:
    // # HELP rx_packets_total Total network packets received
    // # TYPE rx_packets_total counter
    // rx_packets_total 1
}
```

---

## 15. Static Topic-Based Multi-Subscriber Event Fan-Out

Distribute events across multiple decoupled subscribers partitioned by Topic ID without dynamic memory allocation.

```cpp
#include <corium/corium.hpp>
#include <corium/EventRouter.hpp>

struct RadarTrack { uint32_t targetId; float rangeMeters; };
using SystemEvents = std::variant<corium::QuitEvent, RadarTrack>;

corium::EventRouter<SystemEvents, /* MaxSubscribersPerTopic = */ 4, /* MaxTopics = */ 8> router;

// Subscribe Collision Avoidance module to Topic 101 (Radar Stream)
router.subscribeEvent<RadarTrack>(101, [](const RadarTrack& track) {
    if (track.rangeMeters < 50.0f) {
        std::cout << "Collision Warning for Target " << track.targetId << "!\n";
    }
});

// Subscribe Mission Logger to Topic 101
router.subscribeEvent<RadarTrack>(101, [](const RadarTrack& track) {
    std::cout << "Logging Target " << track.targetId << "\n";
});

// Publish track event to Topic 101 (fans out to all 2 subscribers)
router.publishEvent(101, RadarTrack{.targetId = 42, .rangeMeters = 35.0f});
```

---

## 16. Guarded FSM State Transitions with Safety Predicates

Conditionally permit or reject state transitions using compile-time guard predicates.

```cpp
#include <corium/fsm/fsm.hpp>

struct IdleState {};
struct ArmedState {};

struct ArmCommand { bool keyInserted; int batteryPct; };

// Guard predicate evaluated before transition
struct PreFlightSafetyGuard {
    bool operator()(const IdleState&, const ArmCommand& cmd) const noexcept {
        return cmd.keyInserted && cmd.batteryPct > 20;
    }
};

using SecureDroneTable = corium::fsm::TransitionTable<
    corium::fsm::Transition<IdleState, ArmCommand, ArmedState, PreFlightSafetyGuard>
>;

corium::fsm::StateMachine<SecureDroneTable, IdleState, ArmedState> fsm;

void tryArm() {
    // Rejected by guard (battery too low) -> Remains in IdleState
    fsm.process_event(ArmCommand{.keyInserted = true, .batteryPct = 10});
    assert(fsm.is<IdleState>());

    // Accepted by guard -> Transitions to ArmedState
    fsm.process_event(ArmCommand{.keyInserted = true, .batteryPct = 95});
    assert(fsm.is<ArmedState>());
}
```
