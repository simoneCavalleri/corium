// =============================================================================
// Corium Showcase 04: Automotive Steer-by-Wire & Braking ECU (ASIL-D Ready)
// Demonstrates:
//  - WatchdogSupervisor: Multi-task deadline SLA tracking & hardware kick suppression
//  - CircuitBreaker: Active fault isolation (Closed -> Open -> Fallback)
//  - FlightRecorder Profiler: Real post-to-dispatch queue latency measurement
//  - Chrome Tracing / Perfetto JSON flame-chart export
// =============================================================================

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>
#include <variant>

#include "corium/Application.hpp"
#include "corium/BackgroundService.hpp"
#include "corium/Runtime.hpp"
#include "corium/profiler/FlightRecorder.hpp"
#include "corium/safety/CircuitBreaker.hpp"
#include "corium/safety/SafetyEvents.hpp"
#include "corium/safety/WatchdogSupervisor.hpp"

// -----------------------------------------------------------------------------
// 1. Automotive ECU Domain Events
// -----------------------------------------------------------------------------
struct BrakePedalAngleEvent {
    float pedalDisplacementPct; // 0.0 to 100.0%
    float pressureBar;
};

struct InverterTorqueCommandEvent {
    float targetTorqueNm;
    float currentRpm;
};

struct StabilityInterventionEvent {
    float yawRateDegS;
    uint32_t wheelSlipFlags;
};

using AutomotiveEvents = std::variant<
    corium::QuitEvent,
    BrakePedalAngleEvent,
    InverterTorqueCommandEvent,
    StabilityInterventionEvent,
    corium::safety::WatchdogTimeoutEvent
>;

// Zero-heap runtime with FlightRecorder profiler enabled
using AutomotiveRuntime = corium::RuntimeBuilder<>
    ::WithEvents<AutomotiveEvents>
    ::WithPriorityQueue<32, 128>
    ::WithProfiler<corium::profiler::FlightRecorderProfiler<64>> // 64-event circular flight buffer
    ::Build;

// -----------------------------------------------------------------------------
// 2. Automotive Background Tasks (Pedal Sensor & Brake Actuator)
// -----------------------------------------------------------------------------
constexpr uint32_t TASK_ID_PEDAL_SENSOR     = 1;
constexpr uint32_t TASK_ID_BRAKE_ACTUATOR   = 2;
constexpr uint32_t TASK_ID_STABILITY_CTRL   = 3;

class BrakeActuatorService : public corium::ProducerBackgroundService<AutomotiveEvents> {
public:
    void run(std::stop_token stopToken)
    {
        uint32_t cycle = 1;
        while (!stopToken.stop_requested() && cycle <= 4) {
            // Task cycle: nominal 25ms
            std::this_thread::sleep_for(std::chrono::milliseconds(25));

            // Cycle 3: Inject intentional hardware stall exceeding the 40ms watchdog SLA
            if (cycle == 3) {
                std::this_thread::sleep_for(std::chrono::milliseconds(55)); // Deadline violation!
            }

            post(InverterTorqueCommandEvent{150.0f - cycle * 10.0f, 2400.0f});
            cycle++;
        }
    }
};

// -----------------------------------------------------------------------------
// 3. Central ECU Application Core
// -----------------------------------------------------------------------------
class AutomotiveEcuApp : public corium::Application<AutomotiveEcuApp, AutomotiveEvents> {
public:
    static constexpr uint32_t MaxMonitoredTasks = 4;
    static constexpr uint32_t FaultTripThreshold = 2;
    static constexpr uint32_t RecoveryCooldownMs = 100;

    BrakeActuatorService actuatorService;
    corium::safety::WatchdogSupervisor<MaxMonitoredTasks> watchdog;
    corium::safety::CircuitBreaker<FaultTripThreshold, RecoveryCooldownMs> circuitBreaker;

    uint32_t torqueCommandsProcessed = 0;
    bool emergencyFallbackActivated = false;

    template <typename Registry>
    void onConfigureServices(Registry& registry)
    {
        registry.registerService(actuatorService);
    }

    void onRegisterHandlers()
    {
        // 1. Nominal Torque Demand Handler
        on([this](const InverterTorqueCommandEvent& cmd) {
            torqueCommandsProcessed++;
            watchdog.beat(TASK_ID_BRAKE_ACTUATOR); // Heartbeat reported

            if (circuitBreaker.allowExecution()) {
                std::cout << "  [\033[32mBRAKE-BY-WIRE\033[0m] Target Torque: "
                          << cmd.targetTorqueNm << " Nm | Motor RPM: " << cmd.currentRpm << "\n";
                circuitBreaker.recordSuccess();
            } else {
                std::cout << "  [\033[33mHYDRAULIC BACKUP\033[0m] Primary actuator isolated (Circuit Breaker OPEN). Redundant brake engaged!\n";
            }
        });

        // 2. Watchdog Deadline Violation Emergency Handler
        on([this](const corium::safety::WatchdogTimeoutEvent& timeout) {
            emergencyFallbackActivated = true;
            circuitBreaker.recordFailure();
            std::cout << "  [\033[31;1mWATCHDOG VIOLATION\033[0m] Subsystem #" << timeout.serviceId
                      << " exceeded deadline SLA (Budget: " << (timeout.timeoutBudgetNs / 1'000'000) << " ms)!\n";
            std::cout << "  [\033[31;1mSAFETY ACTION\033[0m] Watchdog kick suppressed. Circuit breaker tripped.\n";
        });
    }

    void onInitialize()
    {
        std::cout << "[ECU Core] Initializing ASIL-D Safety Watchdog & Heartbeat Monitor...\n";

        // Configure hardware watchdog kick callback
        watchdog.setWatchdogKickCallback([](void*) {
            std::cout << "  [\033[35mHARDWARE IWDG\033[0m] >>> REFRESH / PET HARDWARE WATCHDOG <<<\n";
        });

        // Register safety tasks and their maximum allowable deadline SLAs
        watchdog.registerService(TASK_ID_PEDAL_SENSOR,   50'000'000); // 50ms SLA
        watchdog.registerService(TASK_ID_BRAKE_ACTUATOR, 40'000'000); // 40ms SLA
        watchdog.registerService(TASK_ID_STABILITY_CTRL, 60'000'000); // 60ms SLA

        // Initial heartbeats
        watchdog.beat(TASK_ID_PEDAL_SENSOR);
        watchdog.beat(TASK_ID_STABILITY_CTRL);
    }
};

int main()
{
    std::cout << "=======================================================\n";
    std::cout << " Corium Showcase 04: Automotive Steer-by-Wire ECU      \n";
    std::cout << " Watchdog Supervisor | Circuit Breaker | FlightRecorder\n";
    std::cout << "=======================================================\n\n";

    AutomotiveRuntime runtime;
    AutomotiveEcuApp app;

    runtime.initialize(app);

    std::cout << "[ECU Core] Running active supervisory control loop:\n\n";

    for (int step = 1; step <= 5; ++step) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // Periodic sensor task heartbeats
        app.watchdog.beat(TASK_ID_PEDAL_SENSOR);
        app.watchdog.beat(TASK_ID_STABILITY_CTRL);

        // Process incoming actuator events
        runtime.pump();

        // Run watchdog supervisor iteration (kicks IWDG if healthy, suppresses and dispatches if failed)
        app.watchdog.supervise(runtime.eventSink());
    }

    std::cout << "\n=======================================================\n";
    std::cout << " [Automotive Safety Supervisor Summary]\n";
    std::cout << "  - Hardware Watchdog Kicks   : " << app.watchdog.totalKicks() << " (Nominal execution)\n";
    std::cout << "  - Watchdog Suppressions     : " << app.watchdog.totalSuppressions() << " (Fault isolated)\n";
    std::cout << "  - Circuit Breaker State     : "
              << (app.circuitBreaker.state() == corium::safety::CircuitState::Open ? "\033[31;1mOPEN (Isolated)\033[0m" : "CLOSED") << "\n";
    std::cout << "=======================================================\n\n";

    // Export execution timeline to Chrome Tracing / Perfetto JSON format
    const std::string traceFile = "flight_trace.json";
    std::ofstream os(traceFile);
    if (os.is_open()) {
        runtime.profiler().flightRecorder().exportChromeTracingJson(os);
        std::cout << "[\033[32mFLIGHT RECORDER\033[0m] Exported nanosecond execution trace to: " << traceFile << "\n";
        std::cout << "  -> Open https://ui.perfetto.dev to view visual flame-chart timeline!\n";
    }

    runtime.shutdown();
    std::cout << "\nShowcase 04 finished successfully.\n";
    return 0;
}
