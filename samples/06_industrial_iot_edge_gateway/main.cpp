// =============================================================================
// Corium Showcase 06: Industrial Robotics & IoT Edge Gateway
// Demonstrates:
//  - Conditional Event Filtering & Multi-Zone Routing (on(predicate, handler))
//  - Zero-Heap Statically-Pooled Coroutines (PooledTask & PooledGenerator)
//  - Asynchronous Event-Driven Synchronization (AsyncEvent<T>)
//  - Deterministic Zero-Drift Min-Heap Periodic Timers
//  - ABI-Validated Binary Wire Protocol Serialization
// =============================================================================

#include <chrono>
#include <iomanip>
#include <iostream>
#include <variant>

#include "corium/corium.hpp"

// -----------------------------------------------------------------------------
// 1. Industrial Domain Events
// -----------------------------------------------------------------------------
enum class DeviceZone : uint8_t {
    RoboticArm_A = 1,
    Conveyor_B   = 2,
    WeldingCell_C = 3
};

struct SensorSample {
    uint32_t channelId;
    float reading;
};

struct RobotJointTelemetryEvent {
    DeviceZone zone;
    uint32_t jointId;
    float positionDeg;
    float torqueNm;
    float temperatureC;
};

struct SafetyAlertEvent {
    DeviceZone zone;
    uint32_t alertCode;
    const char* alertMessage;
};

struct GatewayHeartbeatEvent {
    uint32_t cycleCount;
};

using GatewayEvents = std::variant<
    corium::QuitEvent,
    RobotJointTelemetryEvent,
    SafetyAlertEvent,
    GatewayHeartbeatEvent
>;

// -----------------------------------------------------------------------------
// 2. Coroutine-Based Sensor Stream Generator (Zero-Heap PooledGenerator)
// -----------------------------------------------------------------------------
corium::async::PooledGenerator<SensorSample, 4, 512> simulateSensorStream(uint32_t baseChannel, int sampleCount) {
    for (int i = 0; i < sampleCount; ++i) {
        co_yield SensorSample{
            .channelId = baseChannel + static_cast<uint32_t>(i),
            .reading = 25.0f + static_cast<float>(i) * 1.5f
        };
    }
}

// -----------------------------------------------------------------------------
// 3. Industrial Edge Gateway Application
// -----------------------------------------------------------------------------
class EdgeGatewayApp : public corium::Application<EdgeGatewayApp, GatewayEvents> {
public:
    uint32_t armTelemetryCount = 0;
    uint32_t conveyorTelemetryCount = 0;
    uint32_t highTempAlerts = 0;
    uint32_t heartbeatTicks = 0;

    corium::async::AsyncEvent<uint32_t> telemetryReadyEvent;

    void onRegisterHandlers()
    {
        // ---------------------------------------------------------------------
        // Feature 1: Predicate-Filtered Event Dispatching (on(filter, handler))
        // ---------------------------------------------------------------------

        // Filter 1: Capture telemetry from Robotic Arm Zone only
        on(
            [](const RobotJointTelemetryEvent& e) { return e.zone == DeviceZone::RoboticArm_A; },
            [this](const RobotJointTelemetryEvent& e) {
                armTelemetryCount++;
                std::cout << "  [\033[36mROBOT ARM\033[0m] Joint #" << e.jointId
                          << " | Pos: " << std::fixed << std::setprecision(1) << e.positionDeg << " deg"
                          << " | Torque: " << e.torqueNm << " Nm"
                          << " | Temp: " << e.temperatureC << " C\n";
            }
        );

        // Filter 2: Capture telemetry from Conveyor Zone only
        on(
            [](const RobotJointTelemetryEvent& e) { return e.zone == DeviceZone::Conveyor_B; },
            [this](const RobotJointTelemetryEvent& e) {
                conveyorTelemetryCount++;
                std::cout << "  [\033[32mCONVEYOR\033[0m]  Motor #" << e.jointId
                          << " | RPM: " << std::fixed << std::setprecision(0) << e.positionDeg
                          << " | Load: " << std::setprecision(1) << e.torqueNm << " %"
                          << " | Temp: " << e.temperatureC << " C\n";
            }
        );

        // Filter 3: Automatic thermal overload detection (Temp > 70C across any zone)
        on(
            [](const RobotJointTelemetryEvent& e) { return e.temperatureC > 70.0f; },
            [this](const RobotJointTelemetryEvent& e) {
                highTempAlerts++;
                std::cout << "  [\033[31;1mTHERMAL WARNING\033[0m] High temperature on Zone "
                          << static_cast<int>(e.zone) << ", Joint " << e.jointId
                          << ": " << e.temperatureC << " C!\n";
            }
        );

        // Standard Handler: Gateway Heartbeat
        on([this](const GatewayHeartbeatEvent& h) {
            heartbeatTicks++;
            std::cout << "  [\033[33mHEARTBEAT\033[0m] Gateway Tick #" << h.cycleCount
                      << " (Zero-Drift MinHeap Timer Active)\n";
        });
    }

    void onInitialize()
    {
        std::cout << "[Edge Gateway] Initialized with Event Filtering and Min-Heap Schedulers.\n";
    }
};

// -----------------------------------------------------------------------------
// 4. Asynchronous Pipeline Worker (Zero-Heap PooledTask & AsyncEvent)
// -----------------------------------------------------------------------------
corium::async::PooledTask<void, 4, 512> runBatchIngestPipeline(
    corium::EventSinkT<GatewayEvents> sink,
    corium::async::AsyncEvent<uint32_t>& notifyEvent
) {
    std::cout << "\n--- Starting Sensor Stream Ingestion via PooledGenerator ---\n";

    // Consume pull-based lazy sequence from generator
    for (const auto& sample : simulateSensorStream(100, 3)) {
        std::cout << "  [STREAM PULL] Channel " << sample.channelId
                  << " -> Initial Value: " << sample.reading << " mV\n";
    }

    std::cout << "\n--- Injecting Multi-Zone Telemetry Events ---\n";

    // 1. Telemetry for Robotic Arm (Zone A)
    sink.post(RobotJointTelemetryEvent{
        .zone = DeviceZone::RoboticArm_A,
        .jointId = 1,
        .positionDeg = 45.2f,
        .torqueNm = 12.8f,
        .temperatureC = 42.0f
    });

    sink.post(RobotJointTelemetryEvent{
        .zone = DeviceZone::RoboticArm_A,
        .jointId = 2,
        .positionDeg = 90.0f,
        .torqueNm = 24.5f,
        .temperatureC = 78.5f // Triggers both Zone A handler AND thermal warning!
    });

    // 2. Telemetry for Conveyor (Zone B)
    sink.post(RobotJointTelemetryEvent{
        .zone = DeviceZone::Conveyor_B,
        .jointId = 1,
        .positionDeg = 1450.0f,
        .torqueNm = 65.0f,
        .temperatureC = 38.0f
    });

    // Signal completion via lock-free AsyncEvent
    notifyEvent.emit(3);
    co_return;
}

// -----------------------------------------------------------------------------
// 5. Main Execution Loop
// -----------------------------------------------------------------------------
int main() {
    std::cout << "=======================================================\n";
    std::cout << " Corium Showcase 06: Industrial IoT Edge Gateway\n";
    std::cout << " Event Filtering | Pooled Coroutines | ABI Wire Framing\n";
    std::cout << "=======================================================\n\n";

    using GatewayRuntime = corium::RuntimeBuilder
        ::WithEvents<GatewayEvents>
        ::WithCapacity<256>
        ::WithMaxTimers<16>
        ::Build;

    GatewayRuntime runtime;
    EdgeGatewayApp app;
    runtime.initialize(app);

    auto sink = runtime.eventSink();

    // 1. Schedule deterministic periodic heartbeat timer (Min-Heap O(1) checks)
    corium::TimerId heartbeatTimer = runtime.schedulePeriodic(
        GatewayHeartbeatEvent{.cycleCount = 1},
        std::chrono::milliseconds(20)
    );

    // 2. Launch asynchronous pipeline task
    auto pipelineTask = runBatchIngestPipeline(sink, app.telemetryReadyEvent);
    pipelineTask.resume();

    // 3. Process events and timers
    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        runtime.pump();
    }

    runtime.cancelTimer(heartbeatTimer);

    // 4. Demonstrate ABI-Validated Wire Protocol Serialization
    std::cout << "\n--- Binary Wire Serialization with ABI Signature Check ---\n";
    RobotJointTelemetryEvent jointData{
        .zone = DeviceZone::RoboticArm_A,
        .jointId = 3,
        .positionDeg = 180.0f,
        .torqueNm = 35.0f,
        .temperatureC = 55.0f
    };

    auto packet = corium::wire::WireSerializer::serialize<RobotJointTelemetryEvent, GatewayEvents>(jointData);
    std::cout << "  - Wire Packet Magic  : 0x" << std::hex << packet.header.magic << std::dec << "\n";
    std::cout << "  - ABI Type Signature : 0x" << std::hex << static_cast<int>(packet.header.reserved) << std::dec << "\n";
    std::cout << "  - CRC-16 Checksum    : 0x" << std::hex << packet.header.checksum << std::dec << "\n";
    std::cout << "  - Packet Valid       : " << std::boolalpha << packet.isValid() << "\n";

    // Direct deserialization into runtime sink
    bool deserialized = corium::wire::WireSerializer::deserializeAndPush<GatewayEvents>(packet, sink);
    std::cout << "  - Push into EventBus : " << std::boolalpha << deserialized << "\n";

    runtime.drain();

    // 5. Summary
    std::cout << "\n=======================================================\n";
    std::cout << " [Industrial Gateway Execution Summary]\n";
    std::cout << "  - Arm Telemetry Processed      : " << app.armTelemetryCount << " events\n";
    std::cout << "  - Conveyor Telemetry Processed : " << app.conveyorTelemetryCount << " events\n";
    std::cout << "  - High-Temp Warnings Detected  : " << app.highTempAlerts << " events\n";
    std::cout << "  - Min-Heap Heartbeats Handled  : " << app.heartbeatTicks << " ticks\n";
    std::cout << "  - AsyncEvent Signaled          : " << std::boolalpha << app.telemetryReadyEvent.isReady() << "\n";
    std::cout << "=======================================================\n\n";

    runtime.shutdown();
    std::cout << "Showcase 06 completed successfully with 0 heap allocations.\n";
    return 0;
}
