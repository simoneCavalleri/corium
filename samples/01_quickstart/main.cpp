#include <chrono>
#include <iostream>
#include <thread>
#include <variant>

#include "corium/Application.hpp"
#include "corium/BackgroundService.hpp"
#include "corium/Runtime.hpp"

// 1. Strongly-Typed Domain Events
struct SensorDataEvent {
    int sensorId;
    float temperature;
    float humidity;
};

struct HeartbeatTickEvent {
    uint32_t tickCount;
};

struct CriticalAlertEvent {
    const char* alertMessage;
    int errorCode;
};

using QuickstartEvents = std::variant<
    corium::QuitEvent,
    SensorDataEvent,
    HeartbeatTickEvent,
    CriticalAlertEvent
>;

using QuickstartRuntime = corium::RuntimeBuilder<>
    ::WithEvents<QuickstartEvents>
    ::WithPriorityQueue<128, 512> // 128 High-Priority, 512 Normal slots
    ::WithMaxTimers<16>
    ::Build;

// 2. Background Producer Service (Auto-managed by ServiceRegistry)
class SensorWorkerService : public corium::ProducerBackgroundService<QuickstartEvents> {
public:
    void run(std::stop_token stopToken)
    {
        int count = 1;
        while (!stopToken.stop_requested() && count <= 3) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            // Post normal priority event
            post(SensorDataEvent{1, 22.0f + count * 0.5f, 55.0f});

            if (count == 2) {
                // Post high-priority emergency event directly to high-priority ring buffer
                postHighPriority(CriticalAlertEvent{"High temperature threshold reached!", 101});
            }
            count++;
        }
    }
};

// 3. Application inheriting from Application (Zero-cost CRTP Static Dispatch)
class QuickstartApp : public corium::Application<QuickstartApp, QuickstartRuntime::EventBusType> {
public:
    /// @brief Dedicated lifecycle hook to configure and register background services.
    /// ServiceRegistry automatically injects context, starts threads, and joins on exit.
    template <typename Registry>
    void onConfigureServices(Registry& registry)
    {
        std::cout << "[App] Registering background services in ServiceRegistry...\n";
        registry.registerService(_sensorWorker);
    }

    /// @brief Register static CRTP event handlers.
    void onRegisterHandlers()
    {
        on([this](const SensorDataEvent& e) {
            _sensorEventsReceived++;
            std::cout << "[App] Sensor #" << e.sensorId
                      << " -> Temp: " << e.temperature << " C, Humidity: " << e.humidity << "%\n";
            checkCompletion();
        });

        on([this](const HeartbeatTickEvent& e) {
            _heartbeatTicks++;
            std::cout << "[App] Heartbeat Timer Tick #" << _heartbeatTicks << "\n";
            checkCompletion();
        });

        on([this](const CriticalAlertEvent& e) {
            std::cout << "[App] >>> CRITICAL ALERT [" << e.errorCode << "]: "
                      << e.alertMessage << " <<<\n";
        });

        on([this](const corium::QuitEvent&) {
            std::cout << "[App] QuitEvent received. Shutting down gracefully.\n";
        });
    }

    /// @brief Application initialization hook called after services and handlers are bound.
    void onInitialize()
    {
        std::cout << "[App] Scheduling recurring timers via Application::postPeriodic()...\n";
        _heartbeatTimerId = postPeriodic(HeartbeatTickEvent{1}, std::chrono::milliseconds(50));
    }

    void checkCompletion()
    {
        // When all expected worker and timer events have arrived, gracefully request quit
        if (_sensorEventsReceived >= 3 && _heartbeatTicks >= 2) {
            std::cout << "[App] All tasks completed. Calling this->requestQuit()...\n";
            cancelTimer(_heartbeatTimerId);
            requestQuit();
        }
    }

    /// @brief Application shutdown hook called during runtime.shutdown()
    void onShutdown()
    {
        std::cout << "[App] onShutdown() lifecycle hook called: Releasing app resources.\n";
    }

private:
    SensorWorkerService _sensorWorker;
    corium::TimerId _heartbeatTimerId{corium::INVALID_TIMER_ID};
    int _sensorEventsReceived{0};
    int _heartbeatTicks{0};
};

int main()
{
    std::cout << "=======================================================\n";
    std::cout << " Corium Showcase 01: Quickstart & Core Fundamentals\n";
    std::cout << "=======================================================\n\n";

    QuickstartRuntime runtime;
    QuickstartApp app;

    // Runtime initializes the application, binds handlers, registers services, and starts worker threads
    runtime.initialize(app);

    // Event pump loop running until the application calls requestQuit()
    while (!runtime.quitRequested()) {
        runtime.waitAndPump(std::chrono::milliseconds(30));
    }

    // Graceful shutdown automatically stops and joins all registered services
    runtime.shutdown();
    std::cout << "\nQuickstart sample finished successfully.\n";
    return 0;
}
