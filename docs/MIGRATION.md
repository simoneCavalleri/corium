# Corium Migration Guide

This guide helps developers transition from traditional C++ concurrency patterns (`std::function`, thread pools with mutexes, `boost::asio`, `boost::sml`) to **Corium**'s zero-heap C++20 MPSC architecture.

---

## 1. Migrating from `std::function` Callbacks to CRTP `Application`

### Traditional C++ (Dynamic Allocation & Vtable)
```cpp
// Anti-pattern: allocates on heap, introduces vtable indirect call
std::vector<std::function<void(const SensorData&)>> callbacks;
void registerCallback(std::function<void(const SensorData&)> cb) {
    callbacks.push_back(cb); // Heap allocation
}
```

### Corium Idiom (Static Polymorphism & Type Deduction)
```cpp
#include <corium/corium.hpp>

struct SensorData { float temperature; };
using MyEvents = std::variant<corium::QuitEvent, SensorData>;

class MyApp : public corium::Application<MyApp, MyEvents> {
public:
    // Auto-deduced statically at compile time: 0 heap, 0 vtables
    void onEvent(const SensorData& data) {
        std::cout << "Temp: " << data.temperature << "\n";
    }
};
```

---

## 2. Migrating from Mutex-Locked Queues to Lock-Free Event Sinks

### Traditional C++ (Lock Contention)
```cpp
// Anti-pattern: mutex locking in background threads / ISRs
std::mutex mtx;
std::queue<Event> q;

void producerThread() {
    std::lock_guard<std::mutex> lock(mtx); // Blocks other threads / deadlocks in ISR
    q.push(Event{});
}
```

### Corium Idiom (Lock-Free Vyukov MPSC)
```cpp
class WorkerService : public corium::BackgroundService<MyEvents> {
protected:
    void run(std::stop_token stopToken, corium::EventSinkT<MyEvents> sink) override {
        while (!stopToken.stop_requested()) {
            // Non-blocking, lock-free, zero heap allocation:
            sink.post(SensorData{.temperature = 22.4f});
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
};
```

---

## 3. Migrating from `boost::asio` to Corium Coroutines

| `boost::asio` | `corium::async` | Advantage in Corium |
|---|---|---|
| `asio::awaitable<T>` | `corium::async::Task<T>` | 0 heap allocation on resumption |
| `asio::steady_timer::async_wait` | `corium::async::delay(ms)` | Zero dynamic handler allocation |
| `asio::experimental::make_parallel_group` | `corium::async::whenAll()` / `whenAny()` | Type-safe compile-time tuple unpack |
| Cancellation slots | `corium::async::CancellationToken` | Lock-free atomic cancellation awaiter |

---

## 4. Migrating from `boost::sml` / `tinyfsm` to Corium FSM

```cpp
#include <corium/fsm/fsm.hpp>

struct DisarmedState {};
struct ArmedState { int throttle; };

struct ArmEvent {};
struct ThrottleUpdateEvent { int demand; };

struct UpdateThrottleAction {
    void operator()(ArmedState& s, const ThrottleUpdateEvent& e) const {
        s.throttle = e.demand;
    }
};

// Transition table with external & internal transitions
using MyTable = corium::fsm::TransitionTable<
    corium::fsm::Transition<DisarmedState, ArmEvent, ArmedState>,
    corium::fsm::InternalTransition<ArmedState, ThrottleUpdateEvent, corium::fsm::Always, UpdateThrottleAction>
>;

corium::fsm::StateMachine<MyTable, DisarmedState, ArmedState> fsm;
```

---

## 5. API Mapping Cheat Sheet

| Feature | Traditional Pattern | Corium Equivalent |
|---|---|---|
| Event Dispatch | `std::function<void(E)>` | `corium::Application<Derived>::onEvent(E)` |
| Event Posting | `std::queue<E>` + `std::mutex` | `corium::EventSinkT<EventVariant>::post(e)` |
| High-Priority Alert | Queue sorting / separate mutex | `sink.post(e, corium::EventPriority::High)` |
| Delayed Event | `std::thread` + sleep | `runtime.postDelayed<E>(delay, event)` |
| Hardware ISR Push | Disable IRQ + raw circular queue | `corium::embedded::IsrEventSink::postFromIsr()` |
| Log Formatting | `spdlog` / `printf` | `corium::logging::LoggerT` + `JsonLogSink` |
| Inter-Process IPC | Socket + Protobuf | `corium::ipc::IpcChannel` / `PlatformChannel` |
