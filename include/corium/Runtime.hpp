#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <limits>
#include <utility>

#include "corium/AppCoreContext.hpp"
#include "corium/AppCore.hpp"
#include "corium/EventBus.hpp"
#include "corium/ServiceContext.hpp"
#include "corium/ServiceRegistry.hpp"
#include "corium/internal/ServiceManager.hpp"
#include "corium/internal/VariantIndex.hpp"
#include "corium/policies/Policies.hpp"

namespace corium {

/// @brief Corium Application Runtime managing MPSC event loops and static policy execution.
/// Zero dynamic heap allocations, zero RTTI.
/// @tparam EventVariant The variant type list of supported events.
/// @tparam QueuePolicy Policy governing event queueing (Lock-free MPSC).
/// @tparam SignalPolicy Policy governing notification (NoSignalPolicy default).
/// @tparam StoragePolicy Policy governing handler capacity and delegate inline storage size.
template <
    typename EventVariant = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariant, 1024>,
    typename SignalPolicy = NoSignalPolicy,
    typename StoragePolicy = DefaultStoragePolicy
>
class BasicRuntime {
public:
    using EventBusType = BasicEventBus<EventVariant, QueuePolicy, SignalPolicy, StoragePolicy>;

    enum class State {
        Created,
        Initializing,
        Running,
        Stopping,
        Terminated
    };

    BasicRuntime()
        : _eventBus(),
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

    /// @brief Access current lifecycle state of the runtime.
    [[nodiscard]] State state() const noexcept
    {
        return _state.load(std::memory_order_acquire);
    }

    /// @brief Initialize runtime with target application using static CRTP dispatch.
    /// @tparam Derived Application core type deriving from AppCoreT<Derived, EventBusType>.
    /// @param application Application instance to initialize.
    template <typename Derived>
    void initialize(AppCoreT<Derived, EventBusType>& application)
    {
        _state.store(State::Initializing, std::memory_order_release);
        _appShutdownCb = StaticCallback{
            [](void* appPtr) {
                static_cast<AppCoreT<Derived, EventBusType>*>(appPtr)->shutdown();
            },
            &application
        };

        application.setContext(applicationContext());

        registerCoreHandlers();
        application.registerHandlers();
        _eventBus.seal();

        application.initialize();

        _state.store(State::Running, std::memory_order_release);
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
        std::size_t processed = 0;
        while (_state.load(std::memory_order_relaxed) == State::Running && !_quitRequested && processed < maxEvents) {
            if (!_eventBus.processOne()) {
                break;
            }
            processed++;
        }
    }

    /// @brief Wait for at least one event to become available (or until timeout), then pump all pending events.
    template <typename Rep, typename Period>
    std::size_t waitAndPump(const std::chrono::duration<Rep, Period>& timeout)
    {
        if (_eventBus.empty() && !_quitRequested) {
            _eventBus.signalPolicy().wait_for(timeout);
        }

        std::size_t processed = 0;
        while (_state.load(std::memory_order_relaxed) == State::Running && !_quitRequested) {
            if (!_eventBus.processOne()) {
                break;
            }
            processed++;
        }
        return processed;
    }

    /// @brief Stop runtime cleanly.
    void shutdown() noexcept
    {
        auto st = _state.load(std::memory_order_acquire);
        if (st == State::Stopping || st == State::Terminated) {
            return;
        }

        _state.store(State::Stopping, std::memory_order_release);
        if (_appShutdownCb) {
            _appShutdownCb();
        }
        _state.store(State::Terminated, std::memory_order_release);
    }

    /// @brief Request runtime quit.
    void requestQuit() noexcept
    {
        _quitRequested.store(true, std::memory_order_release);
    }

    /// @brief Check if runtime quit has been requested.
    [[nodiscard]] bool quitRequested() const noexcept
    {
        auto st = _state.load(std::memory_order_acquire);
        return _quitRequested.load(std::memory_order_acquire) || st == State::Stopping || st == State::Terminated;
    }

    /// @brief Set static callback triggered when event queue transitions from empty to non-empty (0 -> 1).
    void setOnQueueNonEmpty(StaticCallback callback)
    {
        _eventBus.setOnQueueNonEmpty(callback);
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

    /// @brief Access event sink handle.
    IEventSinkT<EventVariant> eventSink() noexcept
    {
        return _eventBus.sink();
    }

    /// @brief Access reference to internal event bus.
    EventBusType& eventBus() noexcept
    {
        return _eventBus;
    }

    /// @brief Create AppCoreContext for application wiring.
    AppCoreContextT<EventBusType> applicationContext()
    {
        return AppCoreContextT<EventBusType>{
            _eventBus,
            StaticCallback{
                [](void* ctx) { static_cast<BasicRuntime*>(ctx)->requestQuit(); },
                this
            }
        };
    }

private:
    void registerCoreHandlers()
    {
        if constexpr (has_variant_type_v<QuitEvent, EventVariant>) {
            _eventBus.template registerHandler<QuitEvent>([this](const QuitEvent&) {
                _quitRequested.store(true, std::memory_order_release);
            });
        }
    }

    EventBusType _eventBus;
    StaticCallback _appShutdownCb;
    std::atomic<State> _state{State::Created};
    std::atomic<bool> _quitRequested{false};
};

/// @brief Default Runtime alias using DefaultEvents, NoSignalPolicy, and DefaultStoragePolicy.
using Runtime = BasicRuntime<DefaultEvents, BoundedMpscQueuePolicy<DefaultEvents, 1024>, NoSignalPolicy, DefaultStoragePolicy>;

/// @brief Templated Runtime alias for custom policies.
template <
    typename EventVariant = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariant, 1024>,
    typename SignalPolicy = NoSignalPolicy,
    typename StoragePolicy = DefaultStoragePolicy
>
using RuntimeT = BasicRuntime<EventVariant, QueuePolicy, SignalPolicy, StoragePolicy>;

} // namespace corium

#include "corium/RuntimeBuilder.hpp"
