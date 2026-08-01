#include <corium/corium.hpp>
#include <iostream>

using namespace corium;

struct SensorDataEvent {
    uint32_t sensorId;
    float reading;
};

using AppEvents = std::variant<QuitEvent, SensorDataEvent>;

// Configure Runtime with AuditOverflowPolicy and capacity of 4
using AuditRuntime = RuntimeBuilder<>
    ::WithEvents<AppEvents>
    ::WithCapacity<4>
    ::WithOverflowPolicy<AuditOverflowPolicy>
    ::Build;

class OverflowDemoApp : public AppCore<OverflowDemoApp, AuditRuntime::EventBusType> {
public:
    void onRegisterHandlers()
    {
        on([](const SensorDataEvent& e) {
            std::cout << "  [Processed] Sensor #" << e.sensorId << " Reading: " << e.reading << "V\n";
        });
    }
};

int main()
{
    std::cout << "=========================================================\n";
    std::cout << " Corium Sample 07: Queue Overflow Management Policies\n";
    std::cout << "=========================================================\n\n";

    AuditRuntime runtime;
    OverflowDemoApp app;
    runtime.initialize(app);

    auto sink = runtime.eventSink();

    std::cout << "1. Queue capacity is 4. Enqueueing 10 SensorData events...\n";
    for (uint32_t i = 1; i <= 10; ++i) {
        sink.post(SensorDataEvent{i, 3.3f * static_cast<float>(i)});
    }

    std::cout << "   Total overflowed/dropped events: " 
              << runtime.overflowPolicy().overflowCount() << "\n\n";

    std::cout << "2. Pumping Event Bus...\n";
    runtime.pump();

    std::cout << "\nSample 07 complete.\n";
    runtime.shutdown();
    return 0;
}
