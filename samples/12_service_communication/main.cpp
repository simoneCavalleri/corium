// =============================================================================
// Corium Sample 12 — Service-to-Service & Consumer Background Services
//
// This example demonstrates:
//   1. Creating Consumer BackgroundServices with internal event reactors.
//   2. Producer services sending events directly to consumer services via context().sendToService<TargetService>(evt).
//   3. Consumer services processing incoming events in worker threads using waitAndPump(stopToken, timeout).
//   4. Consumer services relaying status updates back to the Main EventBus.
// =============================================================================

#include <corium/corium.hpp>
#include <chrono>
#include <iostream>
#include <thread>

using namespace corium;

// 1. Data Processor Consumer Service (Receives WorkItemEvents from ProducerService)
class DataProcessorService : public BackgroundService<> {
public:
    DataProcessorService() {
        on([this](const SignalEvent& e) {
            _processedCount++;
            std::cout << "[DataProcessorService] Consumed SignalEvent ID #" << e.id 
                      << " (total processed: " << _processedCount << ")\n";

            // Send notification back to Main Application EventBus
            postEvent(TickEvent{static_cast<double>(_processedCount)});
        });
    }

    void run(std::stop_token stopToken) {
        std::cout << "[DataProcessorService] Worker thread started, awaiting events...\n";
        while (!stopToken.stop_requested()) {
            // Sleep/wait efficiently for incoming events or timeout
            waitAndPump(stopToken, std::chrono::milliseconds(50));
        }
        std::cout << "[DataProcessorService] Worker thread stopping.\n";
    }

    int processedCount() const { return _processedCount; }

private:
    int _processedCount = 0;
};

// 2. Sensor Producer Service (Sends events directly to DataProcessorService)
class SensorProducerService : public BackgroundService<> {
public:
    void run(std::stop_token stopToken) {
        uint32_t signalId = 100;
        std::cout << "[SensorProducerService] Worker thread started.\n";
        while (!stopToken.stop_requested()) {
            signalId++;
            std::cout << "[SensorProducerService] Sending SignalEvent #" << signalId << " to DataProcessorService...\n";

            // Direct Service-to-Service messaging!
            sendToService<DataProcessorService>(SignalEvent{signalId});

            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        std::cout << "[SensorProducerService] Worker thread stopping.\n";
    }
};

// 3. Main Application Wiring Both Services
class ServiceCommunicationApp : public AppCore<ServiceCommunicationApp> {
public:
    DataProcessorService processorService;
    SensorProducerService sensorProducer;

    void onConfigureServices(ServiceRegistry& registry) {
        registry.registerService(processorService);
        registry.registerService(sensorProducer);
    }

    void onRegisterHandlers() {
        on([this](const TickEvent& e) {
            std::cout << "[ServiceCommunicationApp] Main loop received processed notification count: " << e.deltaTime << "\n";
            if (e.deltaTime >= 4.0) {
                std::cout << "[ServiceCommunicationApp] Reached 4 processed items, requesting quit...\n";
                requestQuit();
            }
        });
    }

    void onInitialize() {
        std::cout << "[ServiceCommunicationApp] Initialized successfully.\n";
    }
};

int main() {
    std::cout << "=====================================================\n";
    std::cout << "Corium Sample 12: Service-to-Service Communication\n";
    std::cout << "=====================================================\n";

    Runtime runtime;
    ServiceCommunicationApp app;

    runtime.initialize(app);

    while (!runtime.quitRequested()) {
        runtime.waitAndPump(std::chrono::milliseconds(50));
    }

    runtime.shutdown();
    std::cout << "[Main] Complete.\n";
    return 0;
}
