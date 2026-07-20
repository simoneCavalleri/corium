#include "corium/AppCore.h"

namespace corium {

void AppCore::setContext(AppCoreContext context)
{
    _context = context;
}

AppCore::~AppCore() = default;

EventBusBase& AppCore::events()
{
    return _context.events();
}

IEventSink& AppCore::eventSink()
{
    return _context.eventSink();
}

void AppCore::requestQuit()
{
    _context.requestQuit();
}

void AppCore::configureServices(ServiceRegistry& registry)
{
    if (_state >= State::Configured) {
        return;
    }

    onConfigureServices(registry);
    _state = State::Configured;
}

void AppCore::registerHandlers()
{
    if (_state >= State::Ready) {
        return;
    }

    onRegisterHandlers();
    _state = State::Ready;
}

void AppCore::initialize()
{
    if (_state >= State::Running) {
        return;
    }

    onInitialize();
    _state = State::Running;
}

void AppCore::shutdown()
{
    if (_state == State::Shutdown) {
        return;
    }

    onShutdown();
    _state = State::Shutdown;
}

} // namespace corium
