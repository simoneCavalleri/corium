#include <corium/corium.hpp>
#include <iostream>

using namespace corium;
using namespace corium::logging;

using AppEvents = std::variant<
    QuitEvent,
    LogEvent
>;

using LogRuntime = RuntimeBuilder<>
    ::WithEvents<AppEvents>
    ::Build;

class LoggingApp : public AppCoreT<LoggingApp, LogRuntime::EventBusType> {
public:
    ConsoleLogger consoleLogger{"LoggingApp", LogLevel::Debug};
    LogBackgroundService<sinks::ConsoleLogSink, AppEvents> logService{"AsyncService", LogLevel::Info};

    void onConfigureServices(ServiceRegistry& registry)
    {
        registry.registerService(logService);
    }

    void onRegisterHandlers()
    {
        on([](const LogEvent& event) {
            std::cout << "[EventBus -> Consumer] Received LogEvent: ["
                      << logLevelToString(event.level) << "] "
                      << event.view() << "\n";
        });
    }

    void onInitialize()
    {
        consoleLogger.info("LoggingApp initialized successfully.");
        consoleLogger.debug("Debug information message (level >= Debug).");
        consoleLogger.trace("Trace message (this won't print because minLevel is Debug).");

        // Post LogEvent through Corium's MPSC event bus
        consoleLogger.logToSink(eventSink(), LogLevel::Warn, "Warning event posted through MPSC Event Bus! (val: %d)", 42);
    }
};

int main()
{
    std::cout << "=========================================================\n";
    std::cout << " Corium Sample 11: High-Performance Zero-Heap Logging\n";
    std::cout << "=========================================================\n\n";

    LogRuntime runtime;
    LoggingApp app;

    runtime.initialize(app);

    // Pump events
    runtime.pump();

    std::cout << "\nShutting down LoggingApp...\n";
    runtime.shutdown();

    std::cout << "\nSample 11 complete.\n";
    return 0;
}
