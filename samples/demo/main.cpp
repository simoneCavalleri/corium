// =============================================================================
// Corium Demo — Heartbeat Service
//
// This example demonstrates:
//   1. Defining a background service (HeartbeatService)
//   2. Subclassing AppCore and wiring up event handlers
//   3. Driving the event loop externally via runtime.pump()
//   4. Graceful shutdown via requestQuit()
// =============================================================================

#include <corium/Runtime.h>
#include <corium/AppCore.h>
#include <corium/ServiceRegistry.h>

#include <chrono>
#include <iostream>
#include <thread>

using namespace corium;

// =============================================================================
// 1. Background Service
//
//    A service runs on its own thread and communicates with the application
//    exclusively by posting events into the IEventSink.
//    It must respect the stop_token to allow graceful shutdown.
// =============================================================================

class HeartbeatService : public BackgroundService
{
public:
    using BackgroundService::BackgroundService;

    void run(std::stop_token stopToken) override
    {
        while (!stopToken.stop_requested())
        {
            postEvent(TickEvent{_elapsed});

            _elapsed += _interval;

            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<int>(_interval * 1000)));
        }
    }

private:
    double _elapsed = 0.0;
    double _interval = 1.0;  // seconds
};

// =============================================================================
// 2. Application
//
//    Subclass AppCore and override the lifecycle hooks:
//      - onConfigureServices()  Register background services
//      - onRegisterHandlers()   Subscribe to events
//      - onInitialize()         Application startup logic
//      - onShutdown()           Application cleanup logic
//
//    The handlers run on the dispatch thread (the thread calling pump()),
//    so they are safe to access shared application state without locks.
// =============================================================================

class DemoApp : public AppCore
{
private:
    static constexpr int MaxTicks = 5;

    int _tickCount = 0;

    // -- Lifecycle hooks --

    void onConfigureServices(ServiceRegistry& services) override
    {
        services.addService<HeartbeatService>();
    }

    void onRegisterHandlers() override
    {
        events().registerHandler<TickEvent>(
            [this](const TickEvent& e)
            {
                onTick(e);
            });

        events().registerHandler<QuitEvent>(
            [this](const QuitEvent&)
            {
                std::cout << "Quit signal received.\n";
            });
    }

    void onInitialize() override
    {
        std::cout << "DemoApp started. Waiting for " << MaxTicks << " ticks...\n";
    }

    void onShutdown() override
    {
        std::cout << "DemoApp shutdown complete.\n";
    }

    // -- Event handlers --

    void onTick(const TickEvent& e)
    {
        _tickCount++;

        std::cout
            << "  Tick #" << _tickCount
            << "  (elapsed: " << e.deltaTime << "s)\n";

        if (_tickCount >= MaxTicks)
        {
            std::cout << "Reached " << MaxTicks << " ticks, requesting quit.\n";
            requestQuit();
        }
    }
};

// =============================================================================
// 3. Main — External Loop
//
//    The caller owns the main loop. Corium never blocks; it only processes
//    pending events when pump() is called.
//
//    Typical integration pattern:
//
//      runtime.initialize(app);
//      while (!runtime.quitRequested()) {
//          runtime.pump();
//      }
//      runtime.shutdown();
//
// =============================================================================

#include <condition_variable>
#include <mutex>

int main()
{
    // Corium Policy-Based Runtime Design via RuntimeBuilder:
    // Standard default:
    //   using MyRuntime = RuntimeBuilder<>::Build;
    //
    // Embedded / Real-time Polling with 4096 capacity:
    //   using RealtimeRuntime = RuntimeBuilder<>::WithCapacity<4096>::WithSignalPolicy<NoSignalPolicy>::Build;
    //
    // C++20 Futex (std::atomic::wait):
    //   using FutexRuntime = RuntimeBuilder<>::WithSignalPolicy<AtomicWaitSignalPolicy>::Build;

    Runtime runtime;
    DemoApp app;

    std::mutex loopMutex;
    std::condition_variable loopCv;
    bool hasEvents = false;

    runtime.setOnEventsAvailable([&]() {
        {
            std::lock_guard<std::mutex> lock(loopMutex);
            hasEvents = true;
        }
        loopCv.notify_one();
    });

    try
    {
        runtime.initialize(app);

        while (!runtime.quitRequested())
        {
            {
                std::unique_lock<std::mutex> lock(loopMutex);
                loopCv.wait_for(lock, std::chrono::milliseconds(100), [&]() {
                    return hasEvents || runtime.quitRequested();
                });
                hasEvents = false;
            }

            runtime.pump();
        }

        runtime.shutdown();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Fatal: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
