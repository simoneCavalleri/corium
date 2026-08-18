// =============================================================================
// Corium Showcase 05: Avionics Multi-Process Telemetry & Ground Control IPC
// Demonstrates:
//  - Zero-Copy Binary Wire Protocol Framing (WirePacket + CRC-16)
//  - Inter-Process Communication (IPC) via POSIX Shared Memory (IpcChannel)
//  - UNIX Domain Datagram Sockets (UdsChannel) for discrete command dispatch
//  - Cross-process EventSink type-erased event pumping
// =============================================================================

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>
#include <variant>

#include "corium/Application.hpp"
#include "corium/Runtime.hpp"
#include "corium/ipc/IpcChannel.hpp"
#include "corium/ipc/UdsChannel.hpp"
#include "corium/wire/Serializer.hpp"
#include "corium/wire/WirePacket.hpp"

// -----------------------------------------------------------------------------
// 1. Avionics Telemetry & Ground Control Domain Events (Trivially Copyable)
// -----------------------------------------------------------------------------
struct DroneNavTelemetryEvent {
    uint32_t uavId;
    float latitude;      // deg
    float longitude;     // deg
    float altitudeMsl;   // meters
    float batteryPct;    // %
};

struct GroundFlightCommandEvent {
    uint32_t commandId;  // 1 = Arm, 2 = SetWaypoint, 3 = Land, 4 = RTL
    float targetAltitude;
    float targetSpeed;
};

using AvionicsEvents = std::variant<
    corium::QuitEvent,
    DroneNavTelemetryEvent,
    GroundFlightCommandEvent
>;

// Zero-heap host application runtime
using AvionicsRuntime = corium::RuntimeBuilder
    ::WithEvents<AvionicsEvents>
    ::WithPriorityQueue<32, 128>
    ::Build;

// -----------------------------------------------------------------------------
// 2. Flight Control Core Application
// -----------------------------------------------------------------------------
class FlightCoreApp : public corium::Application<FlightCoreApp, AvionicsEvents> {
public:
    uint32_t telemetryReceived = 0;
    uint32_t commandsExecuted = 0;

    void onRegisterHandlers()
    {
        // 1. Shared Memory High-Rate Telemetry Stream
        on([this](const DroneNavTelemetryEvent& telem) {
            telemetryReceived++;
            std::cout << "  [\033[32mSHM TELEMETRY\033[0m] UAV #" << telem.uavId
                      << " | GPS: [" << std::fixed << std::setprecision(4) << telem.latitude << ", " << telem.longitude << "]"
                      << " | Alt: " << std::setprecision(1) << telem.altitudeMsl << " m"
                      << " | Battery: " << telem.batteryPct << " %\n";
        });

        // 2. UNIX Domain Socket Ground Station Flight Commands
        on([this](const GroundFlightCommandEvent& cmd) {
            commandsExecuted++;
            std::cout << "  [\033[36mUDS COMMAND\033[0m] Command ID #" << cmd.commandId
                      << " | Target Alt: " << cmd.targetAltitude << " m | Target Speed: " << cmd.targetSpeed << " m/s\n";
        });
    }

    void onInitialize()
    {
        std::cout << "[Flight Core] Avionics Core Subsystem online. Listening on IPC channels...\n\n";
    }
};

int main()
{
    std::cout << "=======================================================\n";
    std::cout << " Corium Showcase 05: Avionics Multi-Process Telemetry   \n";
    std::cout << " Binary Wire Protocol | Shared Memory | Domain Sockets \n";
    std::cout << "=======================================================\n\n";

    // -------------------------------------------------------------------------
    // Part 1: Binary Wire Protocol Serialization & CRC-16 Integrity
    // -------------------------------------------------------------------------
    std::cout << "--- Part 1: Zero-Copy Binary Wire Framing (CRC-16) ---\n";

    DroneNavTelemetryEvent sourceTelem{.uavId = 104, .latitude = 45.4642f, .longitude = 9.1900f, .altitudeMsl = 120.5f, .batteryPct = 94.0f};

    // Serialize directly into a framed WirePacket with CRC-16 checksum
    auto packet = corium::wire::WireSerializer::serialize<DroneNavTelemetryEvent, AvionicsEvents>(sourceTelem);

    std::cout << "  - Payload Encoded Size : " << packet.header.payloadLength << " bytes\n";
    std::cout << "  - Total Packet Size    : " << packet.totalWireSize() << " bytes (Magic + Header + CRC16)\n";
    std::cout << "  - CRC-16 Verification  : "
              << (packet.isValid() ? "\033[32m[PASS] CHECKSUM VALID\033[0m" : "\033[31m[FAIL]\033[0m") << "\n\n";

    // -------------------------------------------------------------------------
    // Part 2: Inter-Process Shared Memory & UNIX Domain Socket Simulation
    // -------------------------------------------------------------------------
    std::cout << "--- Part 2: Multi-Process Shared Memory & Socket IPC ---\n";

    const std::string shmName = "/corium_avionics_telem_demo";
    const std::string socketPath = "/tmp/corium_ground_cmd_demo.sock";

    // Initialize IPC Channels
    corium::ipc::IpcChannel<AvionicsEvents, 128> shmChannel;
    if (!shmChannel.create(shmName)) {
        std::cerr << "Warning: Shared memory channel creation failed\n";
    }

    corium::ipc::UdsChannel<AvionicsEvents> udsChannel;
    udsChannel.listen(socketPath);

    AvionicsRuntime runtime;
    FlightCoreApp app;

    runtime.initialize(app);

    // 1. Simulate external sensor daemon writing telemetry into Shared Memory (zero-copy)
    shmChannel.post(DroneNavTelemetryEvent{.uavId = 104, .latitude = 45.4642f, .longitude = 9.1900f, .altitudeMsl = 120.5f, .batteryPct = 94.0f});
    shmChannel.post(DroneNavTelemetryEvent{.uavId = 104, .latitude = 45.4645f, .longitude = 9.1905f, .altitudeMsl = 125.0f, .batteryPct = 93.5f});

    // 2. Simulate Ground Station sending flight commands over UNIX Domain Socket
    corium::ipc::UdsChannel<AvionicsEvents> clientSocket;
    clientSocket.connect(socketPath);
    clientSocket.post(GroundFlightCommandEvent{.commandId = 2, .targetAltitude = 150.0f, .targetSpeed = 18.5f}); // Set Waypoint

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // 3. Pump IPC channels directly into runtime EventSink
    shmChannel.pumpInto(runtime.eventSink());
    udsChannel.pumpInto(runtime.eventSink());

    // 4. Dispatch events on the Flight Core main loop
    runtime.pump();

    std::cout << "\n=======================================================\n";
    std::cout << " [IPC Avionics Link Summary]\n";
    std::cout << "  - SHM Telemetry Packets : " << app.telemetryReceived << " / 2 received\n";
    std::cout << "  - UDS Flight Commands   : " << app.commandsExecuted << " / 1 executed\n";
    std::cout << "=======================================================\n";

    // Clean up demo IPC resources
    shmChannel.unlink();
    runtime.shutdown();

    std::cout << "\nShowcase 05 finished successfully.\n";
    return 0;
}
