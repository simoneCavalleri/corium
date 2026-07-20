#include <stdexcept>
#include <limits>
#include "corium/Runtime.h"
#include "corium/AppCore.h"

namespace corium {

Runtime::Runtime()
    : _eventBus(),
      _serviceContext{ .eventSink = _eventBus },
      _application(nullptr),
      _state(State::Created),
      _quitRequested(false)
{
}

Runtime::~Runtime()
{
    shutdown();
}

void Runtime::registerCoreHandlers()
{
    _eventBus.registerHandler<QuitEvent>([this](const QuitEvent&) {
        _quitRequested = true;
    });
}

void Runtime::initialize(AppCore& application)
{
    if (_state != State::Created) {
        throw std::logic_error("Runtime is already initialized or has been shut down.");
    }

    _application = &application;
    _dispatchThreadId = std::this_thread::get_id();

    bool servicesStarted = false;
    bool appInitialized = false;

    try {
        _application->setContext(applicationContext());
        _application->configureServices(_serviceRegistry);

        _serviceManager.build(_serviceRegistry, _serviceContext);

        registerCoreHandlers();

        _application->registerHandlers();

        _eventBus.seal();

        _serviceManager.start();
        servicesStarted = true;

        _state = State::Running;

        _application->initialize();
        appInitialized = true;
    }
    catch (...) {
        // Clean up only what was completed
        if (servicesStarted) {
            _serviceManager.stop();
            _serviceManager.join();
        }
        if (appInitialized) {
            _application->shutdown();
        }
        _application = nullptr;
        _state = State::Terminated;
        throw;
    }
}

void Runtime::pump()
{
    pump(std::numeric_limits<std::size_t>::max());
}

void Runtime::pump(std::size_t maxEvents)
{
    if (_state != State::Running) {
        throw std::logic_error("Runtime::pump() called when runtime is not running.");
    }

    if (std::this_thread::get_id() != _dispatchThreadId) {
        throw std::logic_error("Runtime::pump() must be called from the dispatch thread (the thread that called initialize()).");
    }

    std::size_t processed = 0;
    while (_state == State::Running && !_quitRequested && processed < maxEvents) {
        if (!_eventBus.processOne()) {
            break;
        }
        processed++;
    }
}

void Runtime::shutdown()
{
    if (_state == State::Terminated) {
        return;
    }

    _serviceManager.stop();
    _serviceManager.join();

    if (_application != nullptr) {
        _application->shutdown();
        _application = nullptr;
    }

    _state = State::Terminated;
}

void Runtime::requestQuit()
{
    _quitRequested = true;
}

bool Runtime::quitRequested() const
{
    return _quitRequested || _state == State::Terminated;
}

void Runtime::setOnEventsAvailable(std::function<void()> callback)
{
    _eventBus.setOnEventsAvailable(std::move(callback));
}


IEventSink& Runtime::eventSink()
{
    return _eventBus;
}

AppCoreContext Runtime::applicationContext()
{
    return AppCoreContext{
        _eventBus,
        _eventBus,
        [this]() { requestQuit(); }
    };
}

} // namespace corium