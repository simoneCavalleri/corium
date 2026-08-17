// =============================================================================
// Corium Showcase 01: Smart Grid Substation Edge Monitor
// Demonstrates:
//  - Application CRTP Lifecycle & ServiceRegistry
//  - Multi-threaded Producer Background Services
//  - C++20 Coroutine Async Tasks (AsyncTask / Delay)
//  - Periodic Timer Scheduling & Priority Anomaly Dispatch
// =============================================================================

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <thread>
#include <variant>

#include "corium/Application.hpp"
#include "corium/BackgroundService.hpp"
#include "corium/Runtime.hpp"
#include "corium/async/Task.hpp"
#include "corium/async/Delay.hpp"

// -----------------------------------------------------------------------------
// 1. Strongly-Typed Telemetry & Domain Events
// -----------------------------------------------------------------------------
struct GridTelemetryEvent {
    uint32_t substationId;
    float voltageRms;       // Volts (Nominal 230V)
    float currentRms;       // Amperes
    float gridFrequencyHz;  // Hz (Nominal 50.00Hz)
};

struct PowerAnalysisReportEvent {
    float activePowerKw;
    float reactivePowerKvar;
    float totalHarmonicDistortion; // %
};

struct DiagnosticTickEvent {
    uint32_t sequence;
};

struct GridAnomalyFaultEvent {
    uint32_t errorCode;
    const char* faultReason;
    float anomalousValue;
};

using SmartGridEvents = std::variant<
    corium::QuitEvent,
    GridTelemetryEvent,
    PowerAnalysisReportEvent,
    DiagnosticTickEvent,
    GridAnomalyFaultEvent
>;

// Build custom zero-heap Runtime specialized for SmartGridEvents
using SmartGridRuntime = corium::RuntimeBuilder
    ::WithEvents<SmartGridEvents>
    ::WithPriorityQueue<64, 256> // 64 High-Priority emergency slots, 256 Normal telemetry slots
    ::WithMaxTimers<16>
    ::Build;

// -----------------------------------------------------------------------------
// 2. High-Frequency Grid Metering Background Service (Sensor Producer)
// -----------------------------------------------------------------------------
class SubstationMeteringService : public corium::ProducerBackgroundService<SmartGridEvents> {
public:
    void run(const std::stop_token& stopToken)
    {
        uint32_t sampleIndex = 1;
        while (!stopToken.stop_requested() && sampleIndex <= 4) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));

            const float voltage = 230.0f + (sampleIndex == 3 ? 24.5f : (sampleIndex * 0.8f));
            const float current = 14.5f + (sampleIndex * 0.3f);
            const float freq = 50.00f + (sampleIndex % 2 == 0 ? 0.02f : -0.01f);

            // Post real-time continuous line telemetry
            post(GridTelemetryEvent{101, voltage, current, freq});

            // Anomaly injection: Sample #3 simulates sudden grid surge
            if (voltage > 250.0f) {
                // Post high-priority emergency event bypassing normal queue
                postHighPriority(GridAnomalyFaultEvent{501, "SURGE_VOLTAGE_LIMIT_EXCEEDED", voltage});
            }

            sampleIndex++;
        }
    }
};

// -----------------------------------------------------------------------------
// 3. Application Core (Lifecycle, Handlers, and C++20 Coroutine Async Tasks)
// -----------------------------------------------------------------------------
class SmartGridSubstationApp : public corium::Application<SmartGridSubstationApp, SmartGridEvents> {
public:
    SubstationMeteringService meteringService;
    uint32_t telemetrySamplesCount = 0;
    uint32_t reportsGenerated = 0;
    uint32_t diagnosticTicksCount = 0;
    bool surgeDetected = false;

    /// @brief Register background services in non-allocating ServiceRegistry.
    template <typename Registry>
    void onConfigureServices(Registry& registry)
    {
        registry.registerService(meteringService);
    }

    /// @brief Compile-time static handler registrations.
    void onRegisterHandlers()
    {
        // Continuous Telemetry Handler
        on([this](const GridTelemetryEvent& e) {
            telemetrySamplesCount++;
            std::cout << "  [\033[32mTELEMETRY\033[0m] Substation #" << e.substationId
                      << " | V: " << std::fixed << std::setprecision(1) << e.voltageRms << " V"
                      << " | I: " << std::fixed << std::setprecision(1) << e.currentRms << " A"
                      << " | Freq: " << std::fixed << std::setprecision(2) << e.gridFrequencyHz << " Hz\n";

            // Spawn and resume non-blocking C++20 coroutine task to compute complex harmonic/power analysis
            auto task = computePowerAnalysisAsync(e.voltageRms, e.currentRms);
            task.resume();
        });

        // Power Analysis Coroutine Result Handler
        on([this](const PowerAnalysisReportEvent& report) {
            reportsGenerated++;
            std::cout << "  [\033[34mANALYTICS\033[0m] Active Power: " << std::fixed << std::setprecision(2)
                      << report.activePowerKw << " kW | Reactive: " << report.reactivePowerKvar
                      << " kvar | THD: " << report.totalHarmonicDistortion << " %\n";
        });

        // Diagnostic Periodic Tick Handler
        on([this](const DiagnosticTickEvent&) {
            diagnosticTicksCount++;
            std::cout << "  [\033[36mDIAGNOSTIC\033[0m] System Heartbeat Tick #" << diagnosticTicksCount
                      << " - Substation Edge Node Operational.\n";
            if (diagnosticTicksCount >= 2 && surgeDetected) {
                std::cout << "\n[App] Mission objectives met. Initiating graceful shutdown...\n";
                requestQuit();
            }
        });

        // High-Priority Critical Anomaly Handler
        on([this](const GridAnomalyFaultEvent& fault) {
            surgeDetected = true;
            std::cout << "  [\033[31;1mCRITICAL ALERT\033[0m] Code " << fault.errorCode
                      << ": " << fault.faultReason << " (Value: " << fault.anomalousValue << " V)!\n";
        });
    }

    /// @brief Post-initialization startup hook: schedule timers and diagnostics.
    void onInitialize()
    {
        std::cout << "[Grid App] Substation Controller initialized. Starting telemetry pipeline...\n\n";

        // Schedule periodic diagnostic ticks every 70 milliseconds
        postPeriodic(DiagnosticTickEvent{1}, std::chrono::milliseconds(70));
    }

    /// @brief Graceful shutdown hook.
    void onShutdown()
    {
        std::cout << "\n[Grid App] Substation Controller offline. Telemetry samples processed: "
                  << telemetrySamplesCount << ", Analytic reports: " << reportsGenerated << "\n";
    }

private:
    // C++20 Coroutine Async Task simulating asynchronous FFT & RMS power calculation
    corium::async::Task<void> computePowerAnalysisAsync(float voltage, float current)
    {
        co_await corium::async::yield();

        const float p = (voltage * current * 0.95f) / 1000.0f; // Active power (kW)
        const float q = (voltage * current * 0.31f) / 1000.0f; // Reactive power (kvar)
        const float thd = 1.25f + std::fmod(voltage, 2.0f);

        // Inject computed analytics back into the event loop via eventSink()
        eventSink().post(PowerAnalysisReportEvent{p, q, thd});
        co_return;
    }
};

int main()
{
    std::cout << "=======================================================\n";
    std::cout << " Corium Showcase 01: Smart Grid Substation Monitor\n";
    std::cout << " Modern C++20 CRTP App | Async Tasks | Priority Anomaly\n";
    std::cout << "=======================================================\n\n";

    SmartGridRuntime runtime;
    SmartGridSubstationApp app;

    runtime.initialize(app);

    // Event pump loop with efficient signal waiting
    while (!runtime.quitRequested()) {
        runtime.waitAndPump(std::chrono::milliseconds(10));
    }

    runtime.shutdown();
    std::cout << "\nShowcase 01 finished successfully.\n";
    return 0;
}
