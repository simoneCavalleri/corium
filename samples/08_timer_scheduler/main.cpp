#include <corium/corium.hpp>
#include <iostream>
#include <thread>
#include <chrono>

using namespace corium;

// Custom events
struct HeartbeatEvent { uint32_t beatCount; };
struct DelayedNotificationEvent { const char* message; };

using AppEvents = std::variant<QuitEvent, HeartbeatEvent, DelayedNotificationEvent>;

using TimerDemoRuntime = RuntimeBuilder<>
    ::WithEvents<AppEvents>
    ::WithMaxTimers<32>
    ::Build;

class TimerDemoApp : public AppCoreT<TimerDemoApp, TimerDemoRuntime::EventBusType> {
public:
    TimerId periodicTimerId = INVALID_TIMER_ID;
    TimerId delayedTimerId = INVALID_TIMER_ID;

    void onRegisterHandlers()
    {
        on([this](const HeartbeatEvent&) {
            _heartbeatsReceived++;
            std::cout << "[Periodic Heartbeat #" << _heartbeatsReceived << "] System healthy.\n";

            if (_heartbeatsReceived >= 4) {
                std::cout << " Stopping periodic heartbeat timer...\n";
                cancelTimer(periodicTimerId);
                requestQuit();
            }
        });

        on([](const DelayedNotificationEvent& e) {
            std::cout << "[Delayed Notification] " << e.message << "\n";
        });
    }

    void onInitialize()
    {
        std::cout << "App initialized. Scheduling timers...\n";
        // Schedule single-shot delayed event after 100ms
        delayedTimerId = postDelayed(DelayedNotificationEvent{"Delayed 100ms timer fired!"}, std::chrono::milliseconds(100));

        // Schedule periodic heartbeat event every 50ms
        periodicTimerId = postPeriodic(HeartbeatEvent{1}, std::chrono::milliseconds(50));
    }

private:
    int _heartbeatsReceived = 0;
};

int main()
{
    std::cout << "=========================================================\n";
    std::cout << " Corium Sample 08: Zero-Heap Timer Scheduler\n";
    std::cout << "=========================================================\n\n";

    TimerDemoRuntime runtime;
    TimerDemoApp app;
    runtime.initialize(app);

    std::cout << "--- Running Event Loop with WaitAndPump ---\n";
    while (!runtime.quitRequested()) {
        runtime.waitAndPump(std::chrono::milliseconds(20));
    }

    runtime.shutdown();
    std::cout << "\nSample 08 complete.\n";
    return 0;
}
