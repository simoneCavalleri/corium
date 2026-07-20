#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <limits>
#include <stdexcept>
#include <thread>

#include "corium/AppCoreContext.h"
#include "corium/AppCore.h"
#include "corium/EventBus.h"
#include "corium/ServiceContext.h"
#include "corium/ServiceRegistry.h"
#include "corium/internal/ServiceManager.h"

namespace corium {

template <size_t Capacity = 1024>
class BasicRuntime {
public:
    enum class State {
        Created,
        Running,
        Terminated
    };

    BasicRuntime()
        : _eventBus(),
          _serviceContext{ .eventSink = _eventBus },
          _application(nullptr),
          _state(State::Created),
          _quitRequested(false)
    {
    }

    ~BasicRuntime()
    {
        shutdown();
    }

    BasicRuntime(const BasicRuntime&) = delete;
    BasicRuntime& operator=(const BasicRuntime&) = delete;

    void initialize(AppCore& application)
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

    void pump()
    {
        pump(std::numeric_limits<std::size_t>::max());
    }

    void pump(std::size_t maxEvents)
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

    void shutdown()
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

    void requestQuit()
    {
        _quitRequested = true;
    }

    [[nodiscard]] bool quitRequested() const
    {
        return _quitRequested || _state == State::Terminated;
    }

    void setOnEventsAvailable(std::function<void()> callback)
    {
        _eventBus.setOnEventsAvailable(std::move(callback));
    }

    IEventSink& eventSink()
    {
        return _eventBus;
    }

    AppCoreContext applicationContext()
    {
        return AppCoreContext{
            _eventBus,
            _eventBus,
            [this]() { requestQuit(); }
        };
    }

private:
    void registerCoreHandlers()
    {
        _eventBus.template registerHandler<QuitEvent>([this](const QuitEvent&) {
            _quitRequested = true;
        });
    }

    BasicEventBus<Capacity> _eventBus;
    ServiceContext _serviceContext;

    ServiceRegistry _serviceRegistry;
    ServiceManager _serviceManager;

    AppCore* _application = nullptr;
    std::thread::id _dispatchThreadId;
    State _state = State::Created;
    std::atomic<bool> _quitRequested{false};
};

template <size_t Capacity = 1024>
using Runtime = BasicRuntime<Capacity>;

} // namespace corium
