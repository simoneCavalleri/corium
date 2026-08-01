#include <corium/corium.hpp>
#include <iostream>
#include <thread>
#include <chrono>

using namespace corium;

// Custom events
struct FrameUpdateEvent { int frameNumber; };
struct EmergencyStopEvent { const char* reason; };

using PriorityAppEvents = std::variant<QuitEvent, FrameUpdateEvent, EmergencyStopEvent>;

// Configure Runtime with PriorityMpscQueuePolicy
using PriorityRuntime = RuntimeBuilder<>
    ::WithEvents<PriorityAppEvents>
    ::WithPriorityQueue<256, 1024>
    ::Build;

class PriorityDemoApp : public AppCore<PriorityDemoApp, PriorityRuntime::EventBusType> {
public:
    void onRegisterHandlers()
    {
        on([](const FrameUpdateEvent& e) {
            std::cout << "  [Normal] Processing Frame #" << e.frameNumber << "\n";
        });

        on([this](const EmergencyStopEvent& e) {
            std::cout << "[HIGH PRIORITY ISR/EMERGENCY] Triggered: " << e.reason << "\n";
            std::cout << "   Halting application loop...\n";
            requestQuit();
        });
    }
};

int main()
{
    std::cout << "=========================================================\n";
    std::cout << " Corium Sample 06: Priority Events & High-Priority ISR\n";
    std::cout << "=========================================================\n\n";

    PriorityRuntime runtime;
    PriorityDemoApp app;
    runtime.initialize(app);

    auto sink = runtime.eventSink();

    std::cout << "1. Enqueueing 5 Normal FrameUpdate events...\n";
    for (int i = 1; i <= 5; ++i) {
        sink.post(FrameUpdateEvent{i});
    }

    std::cout << "2. Enqueueing 1 High-Priority EmergencyStop event (simulating ISR/Interrupt)...\n";
    sink.postHighPriority(EmergencyStopEvent{"Over-temperature threshold exceeded!"});

    std::cout << "3. Enqueueing 2 more Normal FrameUpdate events...\n";
    sink.post(FrameUpdateEvent{6});
    sink.post(FrameUpdateEvent{7});

    std::cout << "\n--- Pumping Event Bus (Notice High-Priority executes FIRST) ---\n";
    runtime.pump();

    runtime.shutdown();
    std::cout << "\nSample 06 complete.\n";
    return 0;
}
