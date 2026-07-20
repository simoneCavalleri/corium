#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <limits>
#include <stdexcept>
#include <thread>

#include "corium/AppCoreContext.hpp"
#include "corium/AppCore.hpp"
#include "corium/EventBus.hpp"
#include "corium/ServiceContext.hpp"
#include "corium/ServiceRegistry.hpp"
#include "corium/internal/ServiceManager.hpp"
#include "corium/internal/VariantIndex.hpp"
#include "corium/policies/Policies.hpp"

namespace corium {

/// @brief Corium Application Runtime managing event loops, policy execution, and background services.
/// @tparam EventVariant The variant type list of supported events.
/// @tparam QueuePolicy Policy governing event queueing (Lock-free MPSC, Blocking queue, etc.).
/// @tparam SignalPolicy Policy governing thread notification (Callback, C++20 Futex, Busy-spin polling, Linux EventFd).
/// @tparam DispatchPolicy Policy governing event dispatching.
template <
    typename EventVariant = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariant, 1024>,
    typename SignalPolicy = CallbackSignalPolicy,
    typename DispatchPolicy = StaticReactorPolicy<EventVariant>
>
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

    /// @brief Initialize runtime with target application.
    /// Builds background services, registers handlers, and seals reactor.
    /// @param application Application instance to initialize.
    void initialize(AppCoreT<EventVariant>& application)
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

    /// @brief Pump all pending events in the queue until empty.
    void pump()
    {
        pump(std::numeric_limits<std::size_t>::max());
    }

    /// @brief Pump up to maxEvents pending events from the queue.
    /// @param maxEvents Maximum number of events to process in this call.
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

    /// @brief Wait for at least one event to become available (or until timeout), then pump all pending events.
    /// @tparam Rep Duration representation type.
    /// @tparam Period Duration period type.
    /// @param timeout Maximum duration to wait before pumping.
    /// @return Number of events processed.
    template <typename Rep, typename Period>
    std::size_t waitAndPump(const std::chrono::duration<Rep, Period>& timeout)
    {
        if (_state != State::Running) {
            throw std::logic_error("Runtime::waitAndPump() called when runtime is not running.");
        }

        if (std::this_thread::get_id() != _dispatchThreadId) {
            throw std::logic_error("Runtime::waitAndPump() must be called from the dispatch thread (the thread that called initialize()).");
        }

        if (_eventBus.empty() && !_quitRequested) {
            _eventBus.signalPolicy().wait_for(timeout);
        }

        std::size_t processed = 0;
        while (_state == State::Running && !_quitRequested) {
            if (!_eventBus.processOne()) {
                break;
            }
            processed++;
        }
        return processed;
    }

    /// @brief Wait indefinitely until an event arrives, then pump pending events.
    /// @return Number of events processed.
    std::size_t waitAndPump()
    {
        return waitAndPump(std::chrono::hours(24 * 365));
    }

    /// @brief Stop background services and shut down application cleanly.
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

    /// @brief Request runtime quit.
    void requestQuit()
    {
        _quitRequested = true;
    }

    /// @brief Check if runtime quit has been requested or if runtime terminated.
    [[nodiscard]] bool quitRequested() const
    {
        return _quitRequested || _state == State::Terminated;
    }

    /// @brief Set callback triggered when event queue transitions from empty to non-empty (0 -> 1).
    /// @param callback Function to invoke on 0->1 queue transition.
    void setOnQueueNonEmpty(std::function<void()> callback)
    {
        _eventBus.setOnQueueNonEmpty(std::move(callback));
    }

    /// @brief Set callback triggered when event queue transitions from empty to non-empty (alias).
    /// @param callback Function to invoke on 0->1 queue transition.
    void onQueueNonEmpty(std::function<void()> callback)
    {
        setOnQueueNonEmpty(std::move(callback));
    }

    /// @brief Legacy callback setter for queue availability (deprecated).
    [[deprecated("Use setOnQueueNonEmpty or onQueueNonEmpty instead.")]]
    void setOnEventsAvailable(std::function<void()> callback)
    {
        setOnQueueNonEmpty(std::move(callback));
    }

    /// @brief Access reference to signal policy.
    SignalPolicy& signalPolicy() noexcept
    {
        return _eventBus.signalPolicy();
    }

    /// @brief Access const reference to signal policy.
    const SignalPolicy& signalPolicy() const noexcept
    {
        return _eventBus.signalPolicy();
    }

    /// @brief Access reference to event sink.
    IEventSinkT<EventVariant>& eventSink()
    {
        return _eventBus;
    }

    /// @brief Create AppCoreContext for application wiring.
    AppCoreContextT<EventVariant> applicationContext()
    {
        return AppCoreContextT<EventVariant>{
            _eventBus,
            _eventBus,
            [this]() { requestQuit(); }
        };
    }

private:
    void registerCoreHandlers()
    {
        if constexpr (has_variant_type_v<QuitEvent, EventVariant>) {
            _eventBus.template registerHandler<QuitEvent>([this](const QuitEvent&) {
                _quitRequested = true;
            });
        }
    }

    BasicEventBus<EventVariant, QueuePolicy, SignalPolicy, DispatchPolicy> _eventBus;
    ServiceContextT<EventVariant> _serviceContext;

    ServiceRegistryT<EventVariant> _serviceRegistry;
    ServiceManager _serviceManager;

    AppCoreT<EventVariant>* _application = nullptr;
    std::thread::id _dispatchThreadId;
    State _state = State::Created;
    std::atomic<bool> _quitRequested{false};
};

/// @brief Default Runtime alias using DefaultEvents and default policies.
template <
    typename EventVariant = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariant, 1024>,
    typename SignalPolicy = CallbackSignalPolicy,
    typename DispatchPolicy = StaticReactorPolicy<EventVariant>
>
using Runtime = BasicRuntime<EventVariant, QueuePolicy, SignalPolicy, DispatchPolicy>;

/// @brief Fluent compile-time builder for configuring BasicRuntime type aliases.
template <
    typename EventVariant = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariant, 1024>,
    typename SignalPolicy = CallbackSignalPolicy,
    typename DispatchPolicy = StaticReactorPolicy<EventVariant>
>
struct RuntimeBuilder {
    /// @brief Specify custom event variant list type.
    template <typename NewEventVariant>
    using WithEvents = RuntimeBuilder<
        NewEventVariant,
        BoundedMpscQueuePolicy<NewEventVariant, 1024>,
        SignalPolicy,
        StaticReactorPolicy<NewEventVariant>
    >;

    /// @brief Specify custom queue capacity for bounded MPSC queue.
    template <size_t Capacity>
    using WithCapacity = RuntimeBuilder<
        EventVariant,
        BoundedMpscQueuePolicy<EventVariant, Capacity>,
        SignalPolicy,
        DispatchPolicy
    >;

    /// @brief Specify custom QueuePolicy.
    template <typename NewQueuePolicy>
    using WithQueuePolicy = RuntimeBuilder<
        EventVariant,
        NewQueuePolicy,
        SignalPolicy,
        DispatchPolicy
    >;

    /// @brief Specify custom SignalPolicy.
    template <typename NewSignalPolicy>
    using WithSignalPolicy = RuntimeBuilder<
        EventVariant,
        QueuePolicy,
        NewSignalPolicy,
        DispatchPolicy
    >;

    /// @brief Specify custom DispatchPolicy.
    template <typename NewDispatchPolicy>
    using WithDispatchPolicy = RuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        NewDispatchPolicy
    >;

    /// @brief Complete builder configuration and return BasicRuntime type.
    using Build = BasicRuntime<EventVariant, QueuePolicy, SignalPolicy, DispatchPolicy>;
};

} // namespace corium
