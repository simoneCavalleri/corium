#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <thread>
#include <variant>

#include "corium/Application.hpp"
#include "corium/BackgroundService.hpp"
#include "corium/Runtime.hpp"
#include "corium/profiler/FlightRecorder.hpp"
#include "corium/profiler/ProfilerPolicies.hpp"
#include "corium/safety/CircuitBreaker.hpp"
#include "corium/safety/SafetyEvents.hpp"
#include "corium/safety/WatchdogSupervisor.hpp"

// Service Identifiers (used for watchdog SLA tracking)
enum ServiceId : uint32_t { Motor = 1, Telemetry = 2 };

// Watchdog supervisor shared across Motor and Telemetry workers
using AppSupervisor = corium::safety::WatchdogSupervisor<4>;

// Domain Events
struct MotorCommandEvent  { int targetRpm; };
struct SensorReadEvent    { float temperature; };

using SafetyAppEvents = std::variant<
    corium::QuitEvent,
    corium::safety::WatchdogTimeoutEvent,
    corium::safety::DeadlineMissedEvent,
    corium::safety::CircuitBreakerTrippedEvent,
    MotorCommandEvent,
    SensorReadEvent
>;

// Configured Runtime with In-Memory Flight Recorder (256 circular trace slots)
using SafetyRuntime = corium::RuntimeBuilder<>
    ::WithEvents<SafetyAppEvents>
    ::WithFlightRecorder<256>
    ::Build;

// ── Motor Controller Worker ───────────────────────────────────────────────────
class MotorService : public corium::ProducerBackgroundService<SafetyAppEvents> {
public:
    explicit MotorService(AppSupervisor& supervisor) : _supervisor(supervisor) {}

    void run(std::stop_token stopToken)
    {
        while (!stopToken.stop_requested()) {
            _supervisor.beat(ServiceId::Motor);
            post(MotorCommandEvent{1500});
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    }
private:
    AppSupervisor& _supervisor;
};

// ── Telemetry Worker ──────────────────────────────────────────────────────────
class TelemetryService : public corium::ProducerBackgroundService<SafetyAppEvents> {
public:
    explicit TelemetryService(AppSupervisor& supervisor) : _supervisor(supervisor) {}

    void run(std::stop_token stopToken)
    {
        while (!stopToken.stop_requested()) {
            _supervisor.beat(ServiceId::Telemetry);
            post(SensorReadEvent{42.5f});
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
    }
private:
    AppSupervisor& _supervisor;
};

// =============================================================================
// Safety Application: encapsulates watchdog, circuit breaker, profiler export
// =============================================================================
class SafetyApp : public corium::Application<SafetyApp, SafetyRuntime::EventBusType> {  // NOLINT
public:
    SafetyApp()
        : _motorWorker(_supervisor),
          _telemetryWorker(_supervisor)
    {}

    // ── Lifecycle Hooks ───────────────────────────────────────────────────────

    /// @brief Register Motor and Telemetry background services in the ServiceRegistry.
    template <typename Registry>
    void onConfigureServices(Registry& registry)
    {
        std::cout << "[Safety Core] Registering background services in ServiceRegistry...\n";
        registry.registerService(_motorWorker);
        registry.registerService(_telemetryWorker);
    }

    /// @brief Register all safety and business event handlers.
    void onRegisterHandlers()
    {
        this->on([](const MotorCommandEvent& e) {
            std::cout << "[Motor Core] Motor RPM: " << e.targetRpm << "\n";
        });

        this->on([](const SensorReadEvent& e) {
            std::cout << "[Telemetry Core] Temperature: " << e.temperature << " C\n";
        });

        this->on([this](const corium::safety::WatchdogTimeoutEvent& e) {
            std::cerr << ">>> [SAFETY ALERT] Watchdog Timeout on Service ID: "
                      << e.serviceId << " <<<\n";
            this->requestQuit();
        });

        this->on([this](const corium::safety::CircuitBreakerTrippedEvent&) {
            std::cerr << ">>> [SAFETY ALERT] Circuit Breaker Tripped! Initiating fail-safe. <<<\n";
            this->requestQuit();
        });
    }

    /// @brief Configure watchdog SLAs and hardware kick callback after services start.
    void onInitialize()
    {
        std::cout << "[Safety Core] Configuring Watchdog Supervisor SLAs...\n";

        // Hardware watchdog kick callback (in real firmware: pet IWDG/WWDG peripheral here)
        _supervisor.setWatchdogKickCallback([](void*) {
            std::cout << "[Hardware Watchdog] >>> KICK / PET IWDG <<<\n";
        });

        // Register monitored services with nanosecond SLA deadlines
        _supervisor.registerService(ServiceId::Motor,     100'000'000); // 100ms
        _supervisor.registerService(ServiceId::Telemetry, 150'000'000); // 150ms
    }

    /// @brief Export the FlightRecorder execution trace to Chrome Tracing JSON format.
    void onShutdown()
    {
        const char* traceFile = "flight_trace.json";
        std::ofstream out(traceFile);
        if (out.is_open() && _exportProfilerFn) {
            _exportProfilerFn(out);
            std::cout << "\n[Flight Recorder] Successfully exported execution trace to: "
                      << traceFile << "\n";
            std::cout << "  (View live flame-charts in https://ui.perfetto.dev or chrome://tracing)\n";
        }
    }

    /// @brief Run one watchdog supervision cycle; call periodically from the main loop.
    void supervise()
    {
        _supervisor.supervise(this->eventSink());
    }

    /// @brief Wire the runtime profiler so onShutdown() can export the trace.
    template <typename Profiler>
    void setProfilerExport(Profiler& profiler)
    {
        _exportProfilerFn = [&profiler](std::ofstream& os) {
            profiler.exportChromeTracingJson(os);
        };
    }

private:
    AppSupervisor    _supervisor;
    MotorService     _motorWorker;
    TelemetryService _telemetryWorker;

    // Type-erased profiler export callback (injected after runtime.initialize())
    std::function<void(std::ofstream&)> _exportProfilerFn;
};

int main()
{
    std::cout << "=======================================================\n";
    std::cout << " Corium Showcase 04: Safety, Watchdog & Flight Recorder\n";
    std::cout << "=======================================================\n\n";

    SafetyRuntime runtime;
    SafetyApp app;

    // initialize():
    //   1. onConfigureServices -> registers Motor & Telemetry workers, starts jthreads
    //   2. onRegisterHandlers  -> binds safety + business event handlers
    //   3. onInitialize        -> configures Watchdog SLAs and hardware kick callback
    runtime.initialize(app);

    // Wire the runtime profiler to the app (accessible only after initialize())
    app.setProfilerExport(runtime.profiler());

    // Supervised event pump loop: app.supervise() checks all service heartbeats
    for (int i = 0; i < 4; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        app.supervise();
        runtime.pump();
    }

    // shutdown():
    //   1. Stops and joins all registered background services
    //   2. Calls app.onShutdown() -> exports FlightRecorder trace to JSON
    runtime.shutdown();
    std::cout << "\nSafety & Profiling showcase finished successfully.\n";
    return 0;
}
