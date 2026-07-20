#pragma once

#include "corium/AppCoreContext.h"
#include "corium/ServiceRegistry.h"
#include "corium/EventBus.h"
#include "corium/IEventSink.h"

#include <cstddef>

namespace corium {

template <size_t Capacity>
class BasicRuntime;

class AppCore {
public:
    virtual ~AppCore();

    AppCore(const AppCore&) = delete;
    AppCore& operator=(const AppCore&) = delete;

    AppCore(AppCore&&) = delete;
    AppCore& operator=(AppCore&&) = delete;

protected:
    AppCore() = default;

    EventBusBase& events();
    IEventSink& eventSink();

    void requestQuit();

private:
    void configureServices(ServiceRegistry& registry);
    void registerHandlers();
    void initialize();
    void shutdown();

private:
    virtual void onConfigureServices(ServiceRegistry& registry) {};
    virtual void onRegisterHandlers() {};
    virtual void onInitialize() = 0;
    virtual void onShutdown() {};

private:
    enum class State {
        Created,
        Configured,
        Ready,
        Running,
        Shutdown
    };

    AppCoreContext _context;

    State _state = State::Created;

    void setContext(AppCoreContext context);

    template <size_t Capacity>
    friend class BasicRuntime;
};

} // namespace corium
