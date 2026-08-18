// =============================================================================
// Corium Bare-Metal ARM Cortex-M Validation Test (QEMU Emulation)
// Target: ARM Cortex-M3 / Cortex-M4 / Cortex-M7
// Built with: arm-none-eabi-g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic
// =============================================================================

#include <cstdio>
#include <cstdint>
#include <variant>

#include "corium/corium.hpp"

// -----------------------------------------------------------------------------
// 1. Hardware Tick Provider for Bare-Metal Microsecond Clock Policy
// -----------------------------------------------------------------------------
struct HardwareTickProvider {
    static uint64_t nowUs() noexcept {
        static uint64_t tick = 0;
        return tick += 1000;
    }
};

// -----------------------------------------------------------------------------
// 2. Domain Events for Bare-Metal Verification
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// 3. Cortex-M Hardware Startup & Semihosting Runtime Initialization
// -----------------------------------------------------------------------------
extern "C" {
    extern uint32_t _sidata;
    extern uint32_t _sdata;
    extern uint32_t _edata;
    extern uint32_t _sbss;
    extern uint32_t _ebss;
    extern uint32_t _estack;

    void __libc_init_array(void) __attribute__((weak));
    void initialise_monitor_handles(void) __attribute__((weak));

    void Default_Handler(void) {
        while (1) {
            __asm__ volatile ("wfi");
        }
    }

    void Reset_Handler(void) {
        // 1. Copy initialized .data section from Flash to SRAM
        uint32_t* src = &_sidata;
        uint32_t* dst = &_sdata;
        while (dst < &_edata) {
            *dst++ = *src++;
        }

        // 2. Zero-initialize .bss section in SRAM
        dst = &_sbss;
        while (dst < &_ebss) {
            *dst++ = 0;
        }

        // 3. Initialize C/C++ runtime & Semihosting IO
        if (initialise_monitor_handles) {
            initialise_monitor_handles();
        }
        if (__libc_init_array) {
            __libc_init_array();
        }

        // 4. Execute application entry point
        int rc = main();

        // 5. Cleanly exit QEMU via ARM Semihosting SYS_EXIT call
        register uint32_t r0 __asm__("r0") = 0x18; // SYS_EXIT
        register uint32_t r1 __asm__("r1") = (rc == 0) ? 0x20026 : 0x20024; // ADP_Stopped_ApplicationExit
        __asm__ volatile ("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");

        while (1) {
            __asm__ volatile ("wfi");
        }
    }
}

// Hardware Interrupt Vector Table at address 0x00000000
__attribute__((section(".vector_table"), used))
void (*const g_pfnVectors[])(void) = {
    reinterpret_cast<void (*)(void)>(&_estack), // 0: Initial Stack Pointer
    Reset_Handler,                              // 1: Reset Handler
    Default_Handler,                            // 2: NMI Handler
    Default_Handler,                            // 3: Hard Fault Handler
    Default_Handler,                            // 4: MPU Fault Handler
    Default_Handler,                            // 5: Bus Fault Handler
    Default_Handler,                            // 6: Usage Fault Handler
};
