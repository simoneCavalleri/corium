#include <iostream>
#include <variant>

#include "corium/Application.hpp"
#include "corium/Runtime.hpp"
#include "corium/policies/OverflowPolicies.hpp"

// High-Throughput Stream Event (trivially copyable)
struct PacketEvent {
    uint32_t sequence;
    uint32_t payload;
};

using StreamEvents = std::variant<
    corium::QuitEvent,
    PacketEvent
>;

// ─── Runtime A: Audit Overflow ────────────────────────────────────────────────
// Tight 4-slot ring buffer with a lock-free atomic dropped-event counter
using AuditedStreamRuntime = corium::RuntimeBuilder<>
    ::WithEvents<StreamEvents>
    ::WithCapacity<4>
    ::WithOverflowPolicy<corium::AuditOverflowPolicy>
    ::Build;

// ─── Runtime B: Fast Batch Stream ────────────────────────────────────────────
using FastStreamRuntime = corium::RuntimeBuilder<>
    ::WithEvents<StreamEvents>
    ::WithCapacity<128>
    ::Build;

// =============================================================================
// Scenario A Application: demonstrates Audit Overflow Policy
// =============================================================================
class AuditedStreamApp : public corium::Application<AuditedStreamApp, AuditedStreamRuntime::EventBusType> {
public:
    int receivedCount = 0;

    /// @brief Register handlers before stream ingestion starts.
    void onRegisterHandlers()
    {
        on([this](const PacketEvent& e) {
            receivedCount++;
            std::cout << "  -> Processed Packet #" << e.sequence << "\n";
        });
    }

    /// @brief Seed the queue with 8 packets on startup (capacity = 4 -> 4 dropped).
    void onInitialize()
    {
        std::cout << "--- Scenario A: Audit Overflow Policy (Capacity: 4, Pushed: 8) ---\n";
        for (uint32_t i = 1; i <= 8; ++i) {
            eventSink().post(PacketEvent{i, i * 100});
        }
    }

    /// @brief Print audit summary on teardown.
    void onShutdown()
    {
        // Note: the dropped count is accessible via runtime.overflowPolicy() in main()
        std::cout << "Total Packets Processed: " << receivedCount << "\n";
    }
};

// =============================================================================
// Scenario B Application: demonstrates high-throughput batch pumping
// =============================================================================
class FastStreamApp : public corium::Application<FastStreamApp, FastStreamRuntime::EventBusType> {
public:
    int receivedCount = 0;

    void onRegisterHandlers()
    {
        on([this](const PacketEvent& e) {
            receivedCount++;
            std::cout << "  -> Processed Packet #" << e.sequence << "\n";
        });
    }

    /// @brief Seed the full 10-packet stream on startup.
    void onInitialize()
    {
        std::cout << "--- Scenario B: High-Throughput Batch Pumping ---\n";
        for (uint32_t i = 1; i <= 10; ++i) {
            eventSink().post(PacketEvent{i, i * 10});
        }
    }

    void onShutdown()
    {
        std::cout << "Remaining Batch Processed successfully.\n";
    }
};

int main()
{
    std::cout << "=======================================================\n";
    std::cout << " Corium Showcase 03: Policies, Queues & Batch Streams  \n";
    std::cout << "=======================================================\n\n";

    // ── Scenario A ────────────────────────────────────────────────────────────
    {
        AuditedStreamRuntime runtime;
        AuditedStreamApp app;
        runtime.initialize(app); // onRegisterHandlers -> onInitialize (seeds queue)

        std::cout << "Pumping pending batch from queue:\n";
        runtime.pump();

        // onShutdown prints packet count; dropped count comes from the runtime policy
        runtime.shutdown();
        std::cout << "Total Packets Dropped  : "
                  << runtime.overflowPolicy().overflowCount() << " (Audit Counter)\n\n";
    }

    // ── Scenario B ────────────────────────────────────────────────────────────
    {
        FastStreamRuntime runtime;
        FastStreamApp app;
        runtime.initialize(app); // onInitialize seeds 10 packets

        // Process a limited chunk of 5 events first, then drain the rest
        std::size_t batch1 = runtime.pumpBatch(5);
        std::cout << "First Batch Chunk Processed : " << batch1 << " events\n";

        runtime.shutdown(); // pump remaining, then call onShutdown
    }

    std::cout << "\nPolicies & Queues showcase finished successfully.\n";
    return 0;
}
