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
event.setCategory("SAFETY_SUPERVISOR");
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
