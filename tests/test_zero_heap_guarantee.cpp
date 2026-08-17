#include <corium/corium.hpp>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>

// Allocation tracking flags
static thread_local bool g_trackAllocations = false;
static thread_local std::size_t g_allocationCount = 0;

#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
#define CORIUM_SANITIZER_ACTIVE 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#define CORIUM_SANITIZER_ACTIVE 1
#endif
#endif

#ifndef CORIUM_SANITIZER_ACTIVE
void* operator new(std::size_t size) {
    if (g_trackAllocations) {
        g_allocationCount++;
    }
    void* ptr = std::malloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}
#endif

using namespace corium;
using namespace corium::wire;

struct ZeroHeapDataEvent {
    uint32_t id;
    float value;
};

using ZeroHeapEvents = std::variant<QuitEvent, ZeroHeapDataEvent>;

int main() {
    std::cout << "=========================================================\n";
    std::cout << " Corium Zero-Heap Hot-Path Allocation Verification Test\n";
    std::cout << "=========================================================\n\n";

    using ZeroHeapRuntime = RuntimeBuilder
        ::WithEvents<ZeroHeapEvents>
        ::WithCapacity<1024>
        ::WithStoragePolicy<CompactStoragePolicy>
        ::WithMaxTimers<32>
        ::Build;

    class App : public Application<App, ZeroHeapRuntime::EventBusType> {
    public:
        uint32_t processedCount = 0;
        float totalValue = 0.0f;

        void onRegisterHandlers() {
            on([this](const ZeroHeapDataEvent& e) {
                processedCount++;
                totalValue += e.value;
            });
        }
    };

    ZeroHeapRuntime runtime;
    App app;
    runtime.initialize(app);

    auto sink = runtime.eventSink();

    // --- BEGIN ZERO-HEAP CRITICAL TEST SECTION ---
    g_allocationCount = 0;
    g_trackAllocations = true;

    // 1. Post 500 events into lock-free ring buffer
    for (uint32_t i = 0; i < 500; ++i) {
        sink.post(ZeroHeapDataEvent{i, 1.5f});
    }

    // 2. Pump events in batch
    runtime.pumpBatch(64, 250);

    // 3. Drain remaining events
    runtime.drain();

    // 4. Schedule and cancel timers
    TimerId t1 = runtime.scheduleDelayed(ZeroHeapDataEvent{999, 0.0f}, std::chrono::milliseconds(100));
    runtime.cancelTimer(t1);

    // 5. Serialize and deserialize WirePacket
    auto packet = WireSerializer::serialize<ZeroHeapDataEvent, ZeroHeapEvents>(ZeroHeapDataEvent{777, 3.14f});
    bool deserialized = WireSerializer::deserializeAndPush<ZeroHeapEvents>(packet, sink);
    runtime.drain();

    g_trackAllocations = false;
    // --- END ZERO-HEAP CRITICAL TEST SECTION ---

    std::cout << "[Test Results]\n";
    std::cout << " Total events processed  : " << app.processedCount << " / 501\n";
    std::cout << " Dynamic heap allocations: " << g_allocationCount << " (Expected: 0)\n";
    std::cout << " Wire deserialization ok : " << std::boolalpha << deserialized << "\n\n";

    if (g_allocationCount != 0 || app.processedCount != 501 || !deserialized) {
        std::cerr << "FAILED: Dynamic memory allocations occurred on the hot-path!\n";
        return 1;
    }

    runtime.shutdown();
    std::cout << "SUCCESS: 0 dynamic allocations on the hot path verified.\n";
    return 0;
}
