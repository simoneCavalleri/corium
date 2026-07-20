#pragma once

#include <atomic>
#include "corium/AppCoreContext.h"
#include "corium/EventBus.h"
#include "corium/internal/ServiceManager.h"
#include "corium/ServiceRegistry.h"

#include "corium/ServiceContext.h"

namespace corium {

class AppCore;

class Runtime {
public:
    enum class State {
        Created,
        Running,
        Terminated
    };

    Runtime();
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    void initialize(AppCore& application);
    void pump();
    void pump(std::size_t maxEvents);
    void shutdown();
    void requestQuit();
    bool quitRequested() const;
    void setOnEventsAvailable(std::function<void()> callback);

    IEventSink& eventSink();

    AppCoreContext applicationContext();

private:
    void registerCoreHandlers();

    EventBus _eventBus;
    ServiceContext _serviceContext;

    ServiceRegistry _serviceRegistry;
    ServiceManager _serviceManager;

    AppCore* _application = nullptr;
    std::thread::id _dispatchThreadId;
    State _state = State::Created;
    std::atomic<bool> _quitRequested{false};
};

} // namespace corium
