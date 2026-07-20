// =============================================================================
// Corium Sample 03 — Custom Event Variant List & Auto-Deduction
//
// This example demonstrates:
//   1. Defining domain-specific event structs (PlayerSpawnEvent, ScoreEvent, etc.).
//   2. Creating a custom compile-time event list std::variant<MyEvents...>.
//   3. Subclassing AppCoreT<MyEvents> with auto-deduced event handlers via on(...).
//   4. Instantiating a custom runtime using RuntimeBuilder<>::WithEvents<MyEvents>::Build.
// =============================================================================

#include <corium/corium.hpp>
#include <iostream>
#include <string>
#include <variant>

using namespace corium;

// 1. Custom User Event Definitions
struct PlayerSpawnEvent {
    int playerId;
    std::string name;
    float posX;
    float posY;
};

struct ScoreChangedEvent {
    int playerId;
    int newScore;
};

struct NetworkPacketEvent {
    uint16_t packetId;
    std::string payload;
};

// 2. Combine into a Custom Event Variant List (including QuitEvent for shutdown)
using GameEvents = std::variant<
    QuitEvent,
    PlayerSpawnEvent,
    ScoreChangedEvent,
    NetworkPacketEvent
>;

// 3. Subclass AppCoreT using GameEvents with auto-deduced on(...) handlers
class GameApp : public AppCoreT<GameEvents> {
protected:
    void onRegisterHandlers() override {
        on([](const PlayerSpawnEvent& e) {
            std::cout << "[GameApp] Player #" << e.playerId << " ('" << e.name 
                      << "') spawned at (" << e.posX << ", " << e.posY << ")\n";
        });

        on([this](const ScoreChangedEvent& e) {
            std::cout << "[GameApp] Player #" << e.playerId << " score updated to " << e.newScore << "\n";
            _eventsProcessed++;
            if (_eventsProcessed >= 3) {
                std::cout << "[GameApp] Processed game events, requesting quit.\n";
                requestQuit();
            }
        });

        on([](const NetworkPacketEvent& e) {
            std::cout << "[GameApp] Network Packet #" << e.packetId << " payload: '" << e.payload << "'\n";
        });
    }

    void onInitialize() override {
        std::cout << "[GameApp] Game initialized with custom EventVariant.\n";
    }

private:
    int _eventsProcessed = 0;
};

// 4. Configure Runtime using RuntimeBuilder
using GameRuntime = RuntimeBuilder<>::WithEvents<GameEvents>::Build;

int main() {
    std::cout << "========================================\n";
    std::cout << "Corium Sample 03: Custom Event Variant List\n";
    std::cout << "========================================\n";

    GameRuntime runtime;
    GameApp app;

    runtime.initialize(app);

    // Post custom events into the event sink
    runtime.eventSink().post(PlayerSpawnEvent{1, "Hero", 100.0f, 250.0f});
    runtime.eventSink().post(NetworkPacketEvent{101, "SYNC_POS"});
    runtime.eventSink().post(ScoreChangedEvent{1, 500});
    runtime.eventSink().post(ScoreChangedEvent{1, 1000});
    runtime.eventSink().post(ScoreChangedEvent{1, 1500});

    while (!runtime.quitRequested()) {
        runtime.pump();
    }

    runtime.shutdown();
    return 0;
}
