#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <limits>
#include <utility>

#include "corium/Application.hpp"
#include "corium/ApplicationContext.hpp"
#include "corium/EventBus.hpp"
#include "corium/ServiceContext.hpp"
#include "corium/ServiceRegistry.hpp"
#include "corium/internal/VariantIndex.hpp"
#include "corium/policies/Policies.hpp"

namespace corium {

/// @brief Corium Application Runtime managing MPSC event loops and static policy execution.
/// Zero dynamic heap allocations, zero RTTI.
/// @tparam EventVariant The variant type list of supported events.
/// @tparam QueuePolicy Policy governing event queueing (Lock-free MPSC).
/// @tparam SignalPolicy Policy governing notification (NoSignalPolicy default).
/// @tparam StoragePolicy Policy governing handler capacity and delegate inline storage size.
/// @tparam OverflowPolicy Policy governing queue overflow handling (DropNewestOverflowPolicy default).
/// @tparam TimerStoragePolicy Policy governing maximum concurrent timers (DefaultTimerStoragePolicy default: 64).
template <
    typename EventVariant = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariant, 1024>,
    typename SignalPolicy = NoSignalPolicy,
    typename StoragePolicy = DefaultStoragePolicy,
    typename OverflowPolicy = DropNewestOverflowPolicy,
    typename TimerStoragePolicy = DefaultTimerStoragePolicy,
    typename ProfilerPolicy = profiler::NullProfiler
>
class BasicRuntime {
public:
    using EventBusType = BasicEventBus<EventVariant, QueuePolicy, SignalPolicy, StoragePolicy, OverflowPolicy, ProfilerPolicy>;
    using ClockPolicyType = typename internal::get_timer_clock_policy<TimerStoragePolicy>::type;
    using TimerSchedulerType = TimerScheduler<EventVariant, TimerStoragePolicy::max_timers, ClockPolicyType>;
    using ProfilerPolicyType = ProfilerPolicy;

    /// @brief Application base class specialization matching this Runtime's EventBus type.
    template <typename Derived, std::size_t MaxServices = 8>
    using Application = corium::Application<Derived, EventBusType, MaxServices>;

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
    /// @tparam Derived Application core type deriving from Application<Derived, EventBusType, MaxServices>.
    /// @tparam MaxServices Number of services the application can register (deduced automatically).
    /// @param application Application instance to initialize.
    template <typename Derived, std::size_t MaxServices = 8>
    void initialize(corium::Application<Derived, EventBusType, MaxServices>& application)
    {
        _state.store(State::Initializing, std::memory_order_release);
        _appShutdownCb = StaticCallback{
            [](void* appPtr) {
                auto* app = static_cast<Derived*>(static_cast<corium::Application<Derived, EventBusType, MaxServices>*>(appPtr));
                app->shutdownServices();
                app->shutdown();
            },
            &application
        };

        auto ctx = applicationContext();
        ctx.setTimerScheduler(_timerScheduler);
        application.setContext(ctx);

        registerCoreHandlers();
        application.registerHandlers();
        _eventBus.seal();

        application.initializeServices(applicationContext().eventSink());
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
        _timerScheduler.processDueTimers(_eventBus);

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
        _timerScheduler.processDueTimers(_eventBus);

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

    /// @brief Pump events in consecutive batches to maximize CPU cache locality.
    /// @param batchSize Number of events to process per batch (default: 16).
    /// @param maxTotal Maximum total number of events to process.
    /// @return Total number of events processed.
    std::size_t pumpBatch(std::size_t batchSize = 16, std::size_t maxTotal = std::numeric_limits<std::size_t>::max())
    {
        _timerScheduler.processDueTimers(_eventBus);

        std::size_t total = 0;
        while (_state.load(std::memory_order_relaxed) == State::Running && !_quitRequested && total < maxTotal) {
            std::size_t toProcess = std::min(batchSize, maxTotal - total);
            std::size_t processed = _eventBus.processBatch(toProcess);
            total += processed;
            if (processed < toProcess) {
                break;
            }
        }
        return total;
    }

    /// @brief Drain and dispatch all currently enqueued events immediately.
    /// @return Total number of events processed.
    std::size_t drain()
    {
        _timerScheduler.processDueTimers(_eventBus);
        if (_state.load(std::memory_order_relaxed) != State::Running || _quitRequested) {
            return 0;
        }
        return _eventBus.drain();
    }

    /// @brief Schedule a single-shot delayed event with std::chrono duration.
    template <typename Rep, typename Period>
    TimerId scheduleDelayed(EventVariant event, const std::chrono::duration<Rep, Period>& delay, EventPriority priority = EventPriority::Normal)
    {
        return _timerScheduler.scheduleDelayed(std::move(event), delay, priority);
    }

    /// @brief Schedule a single-shot delayed event with native clock duration.
    template <typename DurationType>
    TimerId scheduleDelayed(EventVariant event, DurationType delay, EventPriority priority = EventPriority::Normal)
        requires (!std::is_same_v<DurationType, std::chrono::microseconds> && !std::is_same_v<DurationType, std::chrono::milliseconds>)
    {
        return _timerScheduler.scheduleDelayed(std::move(event), delay, priority);
    }

    /// @brief Schedule a recurring periodic event with std::chrono duration.
    template <typename Rep, typename Period>
    TimerId schedulePeriodic(EventVariant event, const std::chrono::duration<Rep, Period>& interval, EventPriority priority = EventPriority::Normal)
    {
        return _timerScheduler.schedulePeriodic(std::move(event), interval, priority);
    }

    /// @brief Schedule a recurring periodic event with native clock duration.
    template <typename DurationType>
    TimerId schedulePeriodic(EventVariant event, DurationType interval, EventPriority priority = EventPriority::Normal)
        requires (!std::is_same_v<DurationType, std::chrono::microseconds> && !std::is_same_v<DurationType, std::chrono::milliseconds>)
    {
        return _timerScheduler.schedulePeriodic(std::move(event), interval, priority);
    }

    /// @brief Cancel an active timer handle.
    bool cancelTimer(TimerId id) noexcept
    {
        return _timerScheduler.cancelTimer(id);
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

    /// @brief Access reference to overflow policy.
    OverflowPolicy& overflowPolicy() noexcept
    {
        return _eventBus.overflowPolicy();
    }

    /// @brief Access const reference to overflow policy.
    const OverflowPolicy& overflowPolicy() const noexcept
    {
        return _eventBus.overflowPolicy();
    }

    /// @brief Access reference to timer scheduler.
    TimerSchedulerType& timerScheduler() noexcept
    {
        return _timerScheduler;
    }

    /// @brief Access const reference to timer scheduler.
    const TimerSchedulerType& timerScheduler() const noexcept
    {
        return _timerScheduler;
    }

    /// @brief Access event sink handle.
    EventSinkT<EventVariant> eventSink() noexcept
    {
        return _eventBus.sink();
    }

    /// @brief Access reference to internal event bus.
    EventBusType& eventBus() noexcept
    {
        return _eventBus;
    }

    /// @brief Access reference to profiler policy.
    ProfilerPolicyType& profiler() noexcept
    {
        return _eventBus.profiler();
    }

    /// @brief Access const reference to profiler policy.
    const ProfilerPolicyType& profiler() const noexcept
    {
        return _eventBus.profiler();
    }

    /// @brief Create ApplicationContext for application wiring.
    ApplicationContext<EventBusType> applicationContext()
    {
        auto ctx = ApplicationContext<EventBusType>{
            _eventBus,
            StaticCallback{
                [](void* c) { static_cast<BasicRuntime*>(c)->requestQuit(); },
                this
            }
        };
        ctx.setTimerScheduler(_timerScheduler);
        return ctx;
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
    TimerSchedulerType _timerScheduler{};
    StaticCallback _appShutdownCb;
    std::atomic<State> _state{State::Created};
    std::atomic<bool> _quitRequested{false};
};

/// @brief Default Runtime alias using DefaultEvents, NoSignalPolicy, DefaultStoragePolicy, DropNewestOverflowPolicy, DefaultTimerStoragePolicy, and NullProfiler.
using Runtime = BasicRuntime<DefaultEvents, BoundedMpscQueuePolicy<DefaultEvents, 1024>, NoSignalPolicy, DefaultStoragePolicy, DropNewestOverflowPolicy, DefaultTimerStoragePolicy, profiler::NullProfiler>;

/// @brief Templated Runtime alias for custom policies.
template <
    typename EventVariant = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariant, 1024>,
    typename SignalPolicy = NoSignalPolicy,
    typename StoragePolicy = DefaultStoragePolicy,
    typename OverflowPolicy = DropNewestOverflowPolicy,
    typename TimerStoragePolicy = DefaultTimerStoragePolicy,
    typename ProfilerPolicy = profiler::NullProfiler
>
using RuntimeT = BasicRuntime<EventVariant, QueuePolicy, SignalPolicy, StoragePolicy, OverflowPolicy, TimerStoragePolicy, ProfilerPolicy>;

} // namespace corium

#include "corium/RuntimeBuilder.hpp"
