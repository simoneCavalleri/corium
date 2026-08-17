#include <cstdint>
#include <iostream>
#include <variant>

#include "corium/Application.hpp"
#include "corium/Runtime.hpp"
#include "corium/embedded/IsrSink.hpp"
#include "corium/policies/StoragePolicies.hpp"
#include "corium/timers/ClockPolicies.hpp"

// Hardware ISR Tick Provider (simulates Cortex-M SysTick / ESP32 timer)
struct HardwareTimer {
    static inline uint64_t currentMicros = 1'000'000ULL;
    static uint64_t getMicroseconds() noexcept { return currentMicros; }
};

// Embedded Domain Events (trivially copyable PODs - zero dynamic allocation)
struct GpioInterruptEvent {
    uint8_t pin;
    bool state;
};

struct AdcSampleEvent {
    uint8_t channel;
    uint16_t rawValue;
};

struct EmergencyStopEvent {
    uint8_t reasonCode;
};

using EmbeddedEvents = std::variant<
    corium::QuitEvent,
    GpioInterruptEvent,
    AdcSampleEvent,
    EmergencyStopEvent
>;

// Ultra-compact embedded runtime: priority queue, custom µs clock, zero-overhead polling
using EmbeddedRuntime = corium::RuntimeBuilder<>
    ::WithEvents<EmbeddedEvents>
    ::WithPriorityQueue<16, 64>                                           // 16 High, 64 Normal priority slots
    ::WithClockPolicy<corium::MicrosecondTickClockPolicy<HardwareTimer>>  // Hardware clock source
    ::WithStoragePolicy<corium::CompactStoragePolicy>                     // 4 handlers/event, 16B inline SBO
    ::WithSignalPolicy<corium::NoSignalPolicy>                            // Zero-overhead polling, no OS primitives
    ::WithMaxTimers<8>
    ::Build;

using EmbeddedIsrSink = corium::embedded::IsrEventSink<corium::EventSinkT<EmbeddedEvents>>;

// Simulated hardware ISR handlers (in real firmware these are IRAM_ATTR / __attribute__((interrupt)))
static void SIMULATED_EXTI0_IRQHandler(EmbeddedIsrSink& isrSink)
{
    // Lock-free, zero-allocation push from interrupt context
    isrSink.postFromIsr(GpioInterruptEvent{5, true});
}

static void SIMULATED_ADC_IRQHandler(EmbeddedIsrSink& isrSink)
{
    isrSink.postFromIsr(AdcSampleEvent{0, 2048});
}

static void SIMULATED_FAULT_IRQHandler(EmbeddedIsrSink& isrSink)
{
    // High-priority event: placed at the front of the dispatch queue
    isrSink.postHighPriorityFromIsr(EmergencyStopEvent{0xFF});
}

// =============================================================================
// Embedded Application: encapsulates hardware lifecycle and ISR event bridging
// =============================================================================
class MicrocontrollerApp : public corium::Application<MicrocontrollerApp, EmbeddedRuntime::EventBusType> {
public:
    // --- Lifecycle Hooks ---

    /// @brief Register event handlers before the super-loop starts.
    void onRegisterHandlers()
    {
        // EmergencyStop is posted with high-priority and dispatched FIRST
        on([](const EmergencyStopEvent& e) {
            std::cout << "[Embedded CPU] >>> EMERGENCY STOP (Code: "
                      << static_cast<int>(e.reasonCode) << ") <<<\n";
        });

        on([](const GpioInterruptEvent& e) {
            std::cout << "[Embedded CPU] GPIO Pin " << static_cast<int>(e.pin)
                      << " transitioned to: " << (e.state ? "HIGH" : "LOW") << "\n";
        });

        on([](const AdcSampleEvent& e) {
            std::cout << "[Embedded CPU] ADC Channel " << static_cast<int>(e.channel)
                      << " -> Raw Value: " << e.rawValue << "\n";
        });
    }

    /// @brief Hardware initialization: create ISR sink and simulate peripheral triggers.
    void onInitialize()
    {
        std::cout << "[Embedded CPU] onInitialize(): Configuring GPIO, ADC, ISR sinks...\n";

        // Create ISR-safe event sink adapter wrapping the runtime event bus
        _isrSink.emplace(eventSink());

        // Simulate peripheral interrupts firing (in real firmware: triggered by hardware)
        std::cout << "[Hardware] Triggering simulated GPIO EXTI Interrupt...\n";
        SIMULATED_EXTI0_IRQHandler(*_isrSink);

        std::cout << "[Hardware] Triggering simulated ADC Conversion Complete...\n";
        SIMULATED_ADC_IRQHandler(*_isrSink);

        std::cout << "[Hardware] Triggering simulated Hardware Watchdog / E-Stop Fault...\n";
        SIMULATED_FAULT_IRQHandler(*_isrSink);
    }

    /// @brief Hardware shutdown: disable peripherals, flush queues, assert safe state.
    void onShutdown()
    {
        std::cout << "[Embedded CPU] onShutdown(): Disabling peripherals, asserting safe state.\n";
        _isrSink.reset();
    }

private:
    // Optional: ISR sink is created in onInitialize once eventSink() is available
    std::optional<EmbeddedIsrSink> _isrSink;
};

int main()
{
    std::cout << "=======================================================\n";
    std::cout << " Corium Showcase 02: Embedded Bare-Metal & Hardware ISR\n";
    std::cout << " Zero-Heap Guarantee | No-RTTI | No-Exceptions Ready   \n";
    std::cout << "=======================================================\n\n";

    EmbeddedRuntime runtime;
    MicrocontrollerApp app;

    // initialize() calls onRegisterHandlers() -> onConfigureServices() -> onInitialize()
    runtime.initialize(app);

    std::cout << "\n[Main Loop] Pumping pending events from bare-metal event queue:\n";
    runtime.pump(); // Non-blocking single batch drain in super-loop

    // shutdown() calls onShutdown() and joins services
    runtime.shutdown();
    std::cout << "\nEmbedded bare-metal showcase finished successfully.\n";
    return 0;
}
