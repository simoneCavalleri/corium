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
