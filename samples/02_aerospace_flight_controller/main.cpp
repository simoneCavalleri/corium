// =============================================================================
// Corium Showcase 02: Aerospace UAV Flight Controller & Hardware ISR
// Demonstrates:
//  - Zero Dynamic Heap Allocation Guarantee (Stack/Static Only)
//  - No-RTTI & No-Exceptions Bare-Metal Cortex-M / RISC-V compatibility
//  - Hardware Interrupt Service Routines (ISRs) via IsrEventSink
//  - Integrated Compile-Time State Machine (FSM) for Autonomous Flight Modes
// =============================================================================

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <variant>

#include <corium/corium.hpp>

// -----------------------------------------------------------------------------
// 1. UAV Flight Controller Event Definitions (Trivially Copyable PODs)
// -----------------------------------------------------------------------------
struct ImuTelemetryEvent {
    int16_t gyroPitch;   // deg/s * 10
    int16_t gyroRoll;    // deg/s * 10
    int16_t accelZ;      // mg
};

struct RcCommandEvent {
    uint16_t throttlePwm; // 1000 - 2000 us
    uint16_t armSwitch;   // 1000 = Disarm, 2000 = Arm
};

struct GpsFixEvent {
    int32_t latitude;    // deg * 1e7
    int32_t longitude;   // deg * 1e7
    uint8_t satellites;
};

struct MotorEmergencyCutoffEvent {
    uint8_t faultSource; // 1 = Hardware Watchdog, 2 = Low Voltage, 3 = Geofence Breach
};

using FlightEvents = std::variant<
    corium::QuitEvent,
    ImuTelemetryEvent,
    RcCommandEvent,
    GpsFixEvent,
    MotorEmergencyCutoffEvent
>;

// Zero-allocation bare-metal runtime
using FlightRuntime = corium::RuntimeBuilder
    ::WithEvents<FlightEvents>
    ::WithPriorityQueue<16, 64> // 16 High-Priority (E-Stop/Failsafe), 64 Normal (Telemetry/RC)
    ::WithStoragePolicy<corium::FixedStoragePolicy<4, 16>> // Max 4 handlers, 16B inline SBO
    ::WithSignalPolicy<corium::NoSignalPolicy>            // Pure polling, zero threading
    ::WithMaxTimers<8>
    ::Build;

using FlightIsrSink = corium::embedded::IsrEventSink<corium::EventSinkT<FlightEvents>>;

// -----------------------------------------------------------------------------
// 2. Flight Mode States & Transition Table
// -----------------------------------------------------------------------------
struct StateDisarmed {
    void onEnter() { std::cout << "  [\033[33mFSM\033[0m] Entering State: \033[33mDISARMED\033[0m (Motors off)\n"; }
};

struct StateInFlight {
    void onEnter() { std::cout << "  [\033[32;1mFSM\033[0m] Entering State: \033[32;1mIN_FLIGHT\033[0m (PID Stabilization active)\n"; }
};

struct StateFailsafeRth {
    void onEnter() { std::cout << "  [\033[31;1mFSM\033[0m] Entering State: \033[31;1mFAILSAFE_RTH\033[0m (Autonomous return-to-home)\n"; }
};

struct ArmGuard {
    bool operator()(const StateDisarmed&, const RcCommandEvent& rc) const {
        return rc.armSwitch > 1500;
    }
};

using FlightTransitionTable = corium::fsm::TransitionTable<
    // Disarmed -> InFlight on RC Arming Command (with ArmGuard check)
    corium::fsm::Transition<StateDisarmed, RcCommandEvent, StateInFlight, ArmGuard>,
    // InFlight -> Failsafe on Emergency Cutoff
    corium::fsm::Transition<StateInFlight, MotorEmergencyCutoffEvent, StateFailsafeRth>
>;

// -----------------------------------------------------------------------------
// 3. Simulated Hardware Peripherals & Interrupt Service Routines (ISRs)
// -----------------------------------------------------------------------------
static void SIMULATED_SPI_IMU_IRQHandler(FlightIsrSink& isrSink)
{
    // Fast interrupt handler pushing 6-DoF sensor readings
    isrSink.postFromIsr(ImuTelemetryEvent{12, -4, 981});
}

static void SIMULATED_UART_SBUS_IRQHandler(FlightIsrSink& isrSink, uint16_t throttle, uint16_t arm)
{
    // RC Receiver DMA completed: dispatch RC command
    isrSink.postFromIsr(RcCommandEvent{throttle, arm});
}

static void SIMULATED_CRITICAL_FAULT_IRQHandler(FlightIsrSink& isrSink, uint8_t fault)
{
    // Hardware E-Stop / Geofence breach: push into high-priority ring buffer
    isrSink.postHighPriorityFromIsr(MotorEmergencyCutoffEvent{fault});
}

// -----------------------------------------------------------------------------
// 4. Flight Controller Application Core
// -----------------------------------------------------------------------------
class UavFlightControllerApp : public corium::Application<UavFlightControllerApp, FlightEvents> {
public:
    corium::fsm::StateMachine<FlightTransitionTable, StateDisarmed, StateInFlight, StateFailsafeRth> fsm;
    uint32_t imuPacketsProcessed = 0;
    uint32_t rcCommandsProcessed = 0;

    void onRegisterHandlers()
    {
        // 1. IMU 6-DoF Telemetry Handler
        on([this](const ImuTelemetryEvent& imu) {
            imuPacketsProcessed++;
            std::cout << "  [\033[32mIMU 1kHz\033[0m] Gyro [Pitch: " << (imu.gyroPitch / 10.0f)
                      << " deg/s, Roll: " << (imu.gyroRoll / 10.0f) << " deg/s]"
                      << " | Accel Z: " << imu.accelZ << " mg\n";
        });

        // 2. RC Pilot Control Handler & FSM Transitions
        on([this](const RcCommandEvent& rc) {
            rcCommandsProcessed++;
            std::cout << "  [\033[36mRC SBUS\033[0m] Throttle: " << rc.throttlePwm
                      << " us | Arm Switch: " << rc.armSwitch << "\n";
            fsm.process_event(rc);
        });

        // 3. High-Priority Emergency E-Stop Handler
        on([this](const MotorEmergencyCutoffEvent& fault) {
            std::cout << "  [\033[31;1mHIGH-PRIORITY ISR\033[0m] Motor E-Stop (Fault Source: "
                      << static_cast<int>(fault.faultSource) << ")!\n";
            fsm.process_event(fault);
        });
    }

    void onInitialize()
    {
        std::cout << "[UAV Core] Initializing Flight Controller (Zero-Heap Mode)...\n";
        _isrSink.emplace(eventSink());
    }

    void onShutdown()
    {
        std::cout << "[UAV Core] Motors Disarmed. Safe state asserted on all PWM channels.\n";
    }

    FlightIsrSink& isrSink() { return *_isrSink; }

private:
    std::optional<FlightIsrSink> _isrSink;
};

int main()
{
    std::cout << "=======================================================\n";
    std::cout << " Corium Showcase 02: Aerospace UAV Flight Controller   \n";
    std::cout << " Zero-Heap Guarantee | Hardware ISR Sinks | Active FSM \n";
    std::cout << "=======================================================\n\n";

    FlightRuntime runtime;
    UavFlightControllerApp app;

    runtime.initialize(app);

    std::cout << "--- Phase 1: Pilot Arming Command & Pre-Flight Self-Test ---\n";
    SIMULATED_UART_SBUS_IRQHandler(app.isrSink(), 1450, 2000); // Arm Switch = 2000
    runtime.pump();

    std::cout << "\n--- Phase 2: High-Rate 1kHz IMU Telemetry Stream in Flight ---\n";
    SIMULATED_SPI_IMU_IRQHandler(app.isrSink());
    SIMULATED_SPI_IMU_IRQHandler(app.isrSink());
    runtime.pump();

    std::cout << "\n--- Phase 3: Hardware Geofence E-Stop (High-Priority ISR Trigger) ---\n";
    SIMULATED_CRITICAL_FAULT_IRQHandler(app.isrSink(), 3); // Critical fault
    runtime.pump();

    std::cout << "\n=======================================================\n";
    std::cout << " [UAV Flight Controller Status Summary]\n";
    std::cout << "  - IMU Telemetry Packets : " << app.imuPacketsProcessed << " processed\n";
    std::cout << "  - RC Commands Processed : " << app.rcCommandsProcessed << " processed\n";
    std::cout << "  - Current Flight State  : "
              << (app.fsm.is<StateFailsafeRth>() ? "\033[31;1mFAILSAFE_RETURN_TO_HOME\033[0m" : "ACTIVE") << "\n";
    std::cout << "=======================================================\n\n";

    runtime.shutdown();
    std::cout << "Showcase 02 finished successfully.\n";
    return 0;
}
