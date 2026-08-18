// =============================================================================
// Corium Bare-Metal ARM Cortex-M Validation Test (QEMU Emulation)
// Target: ARM Cortex-M3 / Cortex-M4 / Cortex-M7
// Built with: arm-none-eabi-g++ -mcpu=cortex-m4 -mthumb -fno-rtti -fno-exceptions
// =============================================================================

#include <cstdio>
#include <cstdint>
#include <variant>

#include "corium/corium.hpp"

struct HardwareTickProvider {
    static uint64_t nowUs() noexcept {
        static uint64_t tick = 0;
        return tick += 1000;
    }
};

struct ArmTelemetryEvent {
    uint32_t sensorId;
    float value;
};

using ArmBareMetalEvents = std::variant<
    corium::QuitEvent,
    ArmTelemetryEvent
>;

using ArmRuntime = corium::RuntimeBuilder
    ::WithEvents<ArmBareMetalEvents>
    ::WithCapacity<64>
    ::WithMaxTimers<8>
    ::WithStoragePolicy<corium::CompactStoragePolicy>
    ::WithClockPolicy<corium::MicrosecondTickClockPolicy<HardwareTickProvider>>
    ::Build;

class BareMetalArmApp : public corium::Application<BareMetalArmApp, ArmBareMetalEvents> {
public:
    uint32_t processedEvents = 0;

    void onRegisterHandlers() {
        on([this](const ArmTelemetryEvent& e) {
            processedEvents++;
            printf("  [ARM ISR SINK] Sensor #%lu -> Value: %d mV\n",
                   static_cast<unsigned long>(e.sensorId),
                   static_cast<int>(e.value * 1000.0f));
        });
    }

    void onInitialize() {
        printf("[BareMetal ARM] Application initialized successfully.\n");
    }
};

int main() {
    printf("=========================================================\n");
    printf(" Corium ARM Cortex-M Bare-Metal Verification (QEMU)\n");
    printf(" Zero-Heap | Zero-RTTI | Compile-Time Static Dispatch\n");
    printf("=========================================================\n\n");

    ArmRuntime runtime;
    BareMetalArmApp app;
    runtime.initialize(app);

    auto sink = runtime.eventSink();

    // 1. Post events into lock-free ring buffer
    sink.post(ArmTelemetryEvent{.sensorId = 101, .value = 3.30f});
    sink.post(ArmTelemetryEvent{.sensorId = 102, .value = 1.80f});
    sink.post(ArmTelemetryEvent{.sensorId = 103, .value = 5.00f});

    // 2. Pump events on single-consumer main loop
    runtime.drain();

    printf("\n[ARM Verification Results]\n");
    printf("  - Total Events Processed : %lu / 3\n", static_cast<unsigned long>(app.processedEvents));

    if (app.processedEvents != 3) {
        printf("FAILED: Event processing mismatch on ARM target!\n");
        return 1;
    }

    runtime.shutdown();
    printf("SUCCESS: Corium bare-metal execution on ARM Cortex-M verified.\n");
    return 0;
}
