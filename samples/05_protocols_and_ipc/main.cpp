#include <chrono>
#include <iostream>
#include <thread>
#include <variant>

#include "corium/Application.hpp"
#include "corium/BackgroundService.hpp"
#include "corium/Runtime.hpp"
#include "corium/ipc/IpcChannel.hpp"
#include "corium/ipc/UdsChannel.hpp"
#include "corium/wire/Serializer.hpp"
#include "corium/wire/WirePacket.hpp"

// Domain events exchanged over Wire & IPC channels
struct MotorTelemetryEvent {
    uint32_t motorId;
    float currentRpm;
    float torqueNm;
};

struct SetSpeedCommand {
    int targetRpm;
};

using NetworkIpcEvents = std::variant<
    corium::QuitEvent,
    MotorTelemetryEvent,
    SetSpeedCommand
>;

using NetworkRuntime = corium::RuntimeBuilder<>
    ::WithEvents<NetworkIpcEvents>
    ::Build;

// =============================================================================
// Robot Host Application
// Encapsulates IPC channel lifecycle in onInitialize/onShutdown hooks.
// Telemetry draining is exposed as a method called from the main loop
// (IPC channels are I/O resources, not background tasks).
// =============================================================================
class RobotHostApp : public corium::Application<RobotHostApp, NetworkRuntime::EventBusType> {
public:
    // ── Lifecycle Hooks ───────────────────────────────────────────────────────

    /// @brief Register business event handlers.
    void onRegisterHandlers()
    {
        // High-frequency telemetry via Shared Memory
        this->on([this](const MotorTelemetryEvent& e) {
            _telemetryCount++;
            std::cout << "[Host App] (SHM) Motor " << e.motorId
                      << " -> Speed: " << e.currentRpm << " RPM, Torque: "
                      << e.torqueNm << " Nm\n";
        });

        // Discrete command via UNIX Domain Socket
        this->on([this](const SetSpeedCommand& cmd) {
            _commandsCount++;
            std::cout << "[Host App] (UDS) SetSpeedCommand received -> Target: "
                      << cmd.targetRpm << " RPM\n";
        });
    }

    /// @brief Open IPC channels (SHM segment + UDS listener socket).
    void onInitialize()
    {
        std::cout << "[Host App] Initializing IPC Channels (Shared Memory & Domain Socket)...\n";
        _shmChannel.create("/corium_showcase_shm");
        _udsChannel.listen("/tmp/corium_showcase_cmd.sock", /*reuseAddr=*/true);
    }

    /// @brief Release IPC resources (unlink SHM, close socket) on graceful shutdown.
    void onShutdown()
    {
        std::cout << "[Host App] Cleaning up IPC channels...\n";
        _shmChannel.unlink();
        _udsChannel.close();
    }

    /// @brief Drain pending IPC data into the main event bus (called from main loop).
    /// IPC channels are I/O resources; draining them is the responsibility of the
    /// host loop, not a background thread, to avoid adding synchronization overhead.
    void pumpIpcChannels()
    {
        _shmChannel.pumpInto(this->eventSink());
        _udsChannel.pumpInto(this->eventSink());
    }

    [[nodiscard]] int telemetryCount() const noexcept { return _telemetryCount; }
    [[nodiscard]] int commandsCount()  const noexcept { return _commandsCount; }

private:
    corium::ipc::IpcChannel<NetworkIpcEvents, 256> _shmChannel;
    corium::ipc::UdsChannel<NetworkIpcEvents>       _udsChannel;
    int _telemetryCount{0};
    int _commandsCount{0};
};

int main()
{
    std::cout << "=======================================================\n";
    std::cout << " Corium Showcase 05: Wire Protocol & Multi-Process IPC \n";
    std::cout << "=======================================================\n\n";

    // --- Part 1: Binary Wire Protocol with CRC16 Verification ---
    std::cout << "--- Part 1: Binary Wire Protocol Serialization ---\n";
    MotorTelemetryEvent outgoingTelemetry{1, 3000.0f, 5.5f};
    auto packet = corium::wire::WireSerializer::serialize<MotorTelemetryEvent, NetworkIpcEvents, 64>(outgoingTelemetry);
    std::cout << "Wire Packet Size  : " << packet.totalWireSize() << " bytes\n";
    std::cout << "Packet CRC16 Valid: " << (packet.isValid() ? "YES (Valid)" : "NO (Corrupted)") << "\n\n";

    // --- Part 2: Multi-Process IPC (Shared Memory & UNIX Domain Sockets) ---
    std::cout << "--- Part 2: Inter-Process Communication (SHM & UDS) ---\n";

    // Clean up any stale SHM segment from a previous run
    corium::ipc::SharedMemory::unlink("/corium_showcase_shm");

    NetworkRuntime runtime;
    RobotHostApp app;

    // initialize():
    //   1. onRegisterHandlers -> binds MotorTelemetryEvent & SetSpeedCommand handlers
    //   2. onInitialize       -> creates SHM segment and opens UDS listener
    runtime.initialize(app);

    // External Client Process (simulates an independent GUI / CLI process)
    std::jthread clientProcess([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        corium::ipc::IpcChannel<NetworkIpcEvents, 256> shmClient;
        shmClient.attach("/corium_showcase_shm");

        corium::ipc::UdsChannel<NetworkIpcEvents> udsClient;
        udsClient.connect("/tmp/corium_showcase_cmd.sock");

        // Push two telemetry samples into the Shared Memory ring buffer
        shmClient.post(MotorTelemetryEvent{1, 1500.0f, 3.2f});
        shmClient.post(MotorTelemetryEvent{1, 1550.0f, 3.4f});

        // Send a speed command via UNIX Domain Socket
        udsClient.post(SetSpeedCommand{2400});

        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    });

    // Main host loop:
    //   1. app.pumpIpcChannels() drains SHM + UDS into the Corium event bus
    //   2. runtime.pump()       dispatches all buffered events to registered handlers
    for (int i = 0; i < 4; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        app.pumpIpcChannels();
        runtime.pump();
    }

    clientProcess.join();

    std::cout << "\n--- Summary ---\n";
    std::cout << "Telemetry Events Processed : " << app.telemetryCount() << " / 2\n";
    std::cout << "Command Events Processed   : " << app.commandsCount()  << " / 1\n";

    // shutdown(): calls app.onShutdown() -> unlinks SHM, closes UDS socket
    runtime.shutdown();
    std::cout << "\nProtocols & IPC showcase finished successfully.\n";
    return 0;
}
