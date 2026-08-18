// =============================================================================
// Corium - High-Performance Zero-Heap C++20 MPSC Event Framework
// Single-Header Standalone Amalgamated Distribution
//
// Generated automatically by tools/amalgamate.py. DO NOT EDIT DIRECTLY.
// MIT License - Copyright (c) 2026 Simone Cavalleri
// =============================================================================

#pragma once


// >>> Begin: corium/corium.hpp
/**
 * @file corium.hpp
 * @ingroup core
 * @brief Master umbrella header for the entire Corium runtime framework.
 */


// IWYU pragma: begin_exports

// >>> Begin: corium/Runtime.hpp
/**
 * @file Runtime.hpp
 * @ingroup core
 * @brief Deterministic single-consumer event loop coordinator and runner.
 */


#include <atomic>
#include <chrono>
#include <cstddef>
#include <limits>
#include <utility>


// >>> Begin: corium/ApplicationContext.hpp
/**
 * @file ApplicationContext.hpp
 * @ingroup core
 * @brief Type-erased context for application runtime introspection and lifecycle control.
 */


#include <array>
#include <chrono>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>


// >>> Begin: corium/Events.hpp
/**
 * @file Events.hpp
 * @ingroup core
 * @brief Standard lifecycle events (QuitEvent, ErrorEvent, TimerEvent).
 */


#include <cstdint>
#include <variant>

namespace corium {

/// @brief Application shutdown event.
struct QuitEvent {};

/// @brief Periodic heartbeat or hardware timer tick event.
struct TickEvent {
    uint64_t tick = 0;
    double deltaTime = 0.0;

    constexpr TickEvent() = default;
    constexpr explicit TickEvent(double dt, uint64_t t = 0)
        : tick(t), deltaTime(dt) {}
};

/// @brief Logical update or execution step event.
struct UpdateEvent {
    double deltaTime = 0.0;

    constexpr UpdateEvent() = default;
    constexpr explicit UpdateEvent(double dt)
        : deltaTime(dt) {}
};

/// @brief System, hardware, or framework error event.
struct ErrorEvent {
    uint32_t code = 0;
    uintptr_t payload = 0;

    constexpr ErrorEvent() = default;
    constexpr ErrorEvent(uint32_t errCode, uintptr_t data = 0)
        : code(errCode), payload(data) {}
};

/// @brief Generic signal or notification trigger event.
struct SignalEvent {
    uint32_t id = 0;

    constexpr SignalEvent() = default;
    constexpr explicit SignalEvent(uint32_t signalId)
        : id(signalId) {}
};

/// @brief Default variant list of core Corium events.
using DefaultEvents = std::variant<
    QuitEvent,
    TickEvent,
    UpdateEvent,
    ErrorEvent,
    SignalEvent
>;

/// @brief Alias for DefaultEvents.
using Event = DefaultEvents;

} // namespace corium

// <<< End: corium/Events.hpp

// >>> Begin: corium/EventSink.hpp
/**
 * @file EventSink.hpp
 * @ingroup core
 * @brief Non-allocating type-erased fat pointer handle for lock-free event posting.
 */


#include <type_traits>
#include <utility>

// >>> Begin: corium/policies/QueuePolicies.hpp
/**
 * @file QueuePolicies.hpp
 * @ingroup policies
 * @brief Bounded and multi-tier priority MPSC queueing policies.
 */


#include <cstddef>
#include <cstdint>
#include <queue>
#include <type_traits>
#include <utility>

#if defined(_WIN32) || defined(_WIN64) || defined(__unix__) || defined(__APPLE__) || (defined(_GLIBCXX_HAS_GTHREADS) && _GLIBCXX_HAS_GTHREADS) || defined(_LIBCPP_HAS_THREAD_API_PTHREAD)
#include <mutex>
#ifndef CORIUM_HAS_STD_MUTEX
#define CORIUM_HAS_STD_MUTEX 1
#endif
#else
#ifndef CORIUM_HAS_STD_MUTEX
#define CORIUM_HAS_STD_MUTEX 0
#endif
#endif


// >>> Begin: corium/MpscRingBuffer.hpp
/**
 * @file MpscRingBuffer.hpp
 * @ingroup core
 * @brief Lock-free Multi-Producer Single-Consumer (MPSC) bounded ring buffer based on Dmitry Vyukov's algorithm.
 */


#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

#if !defined(CORIUM_CACHE_LINE_SIZE)
#if defined(CORIUM_COMPACT_MEMORY) || defined(CORIUM_DISABLE_CACHE_ALIGNMENT)
#define CORIUM_CACHE_LINE_SIZE alignof(std::max_align_t)
#else
#define CORIUM_CACHE_LINE_SIZE 64
#endif
#endif

namespace corium {

/// @ingroup core
/// @brief Lock-free Multiple-Producer, Single-Consumer (MPSC) RingBuffer.
/// Implements Dmitry Vyukov's algorithm with zero heap allocations (uses std::array).
/// Cache-line aligned (alignas(CORIUM_CACHE_LINE_SIZE)) to eliminate false sharing.
/// @tparam T Event element type stored in ring cells.
/// @tparam Capacity Buffer capacity (must be a power of 2).
template <typename T, std::size_t Capacity>
class MpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2.");

    struct alignas(CORIUM_CACHE_LINE_SIZE) Cell {
        std::atomic<std::size_t> sequence;
        alignas(alignof(T)) std::byte storage[sizeof(T)];

        template <typename... Args>
        void construct(Args&&... args) {
            new (static_cast<void*>(storage)) T(std::forward<Args>(args)...);
        }

        [[nodiscard]] T& value() noexcept {
            return *std::launder(reinterpret_cast<T*>(storage));
        }

        [[nodiscard]] const T& value() const noexcept {
            return *std::launder(reinterpret_cast<const T*>(storage));
        }

        void destroy() noexcept {
            value().~T();
        }
    };

public:
    struct PushResult {
        bool pushed;
        bool wasEmpty;
    };

    MpscRingBuffer() {
        for (std::size_t i = 0; i < Capacity; ++i) {
            _buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
        _enqueuePos.store(0, std::memory_order_relaxed);
        _dequeuePos.store(0, std::memory_order_relaxed);
    }

    ~MpscRingBuffer() {
        T dummy;
        while (tryPop(dummy)) {}
    }

    MpscRingBuffer(const MpscRingBuffer&) = delete;
    MpscRingBuffer& operator=(const MpscRingBuffer&) = delete;

    MpscRingBuffer(MpscRingBuffer&&) = delete;
    MpscRingBuffer& operator=(MpscRingBuffer&&) = delete;

    /// @brief Push an item into the queue (Multi-Producer thread safe).
    /// @return PushResult indicating success and whether queue was empty before push.
    template <typename... Args>
    PushResult tryPush(Args&&... args) {
        Cell* cell = nullptr;
        std::size_t pos = _enqueuePos.load(std::memory_order_relaxed);

        for (;;) {
            cell = &_buffer[pos & Mask];
            std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                if (_enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break; // Reserved cell
                }
            } else if (diff < 0) {
                return {false, false}; // Full
            } else {
                pos = _enqueuePos.load(std::memory_order_relaxed);
            }
        }

        cell->construct(std::forward<Args>(args)...);
        cell->sequence.store(pos + 1, std::memory_order_release);

        const bool wasEmpty = (pos == _dequeuePos.load(std::memory_order_relaxed));
        return {true, wasEmpty};
    }

    /// @brief Pop an item from the queue (Single-Consumer only).
    /// @return true if an item was successfully popped; false if empty.
    bool tryPop(T& result) {
        Cell* cell = nullptr;
        std::size_t pos = _dequeuePos.load(std::memory_order_relaxed);

        cell = &_buffer[pos & Mask];
        std::size_t seq = cell->sequence.load(std::memory_order_acquire);
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

        if (diff == 0) {
            _dequeuePos.store(pos + 1, std::memory_order_relaxed);
            result = std::move(cell->value());
            cell->destroy();
            cell->sequence.store(pos + Mask + 1, std::memory_order_release);
            return true;
        }

        return false;
    }

    /// @brief Check if the queue is empty (approximate if concurrent producers are active).
    [[nodiscard]] bool empty() const noexcept {
        const std::size_t pos = _dequeuePos.load(std::memory_order_relaxed);
        const Cell* cell = &_buffer[pos & Mask];
        const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
        const intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
        return diff != 0;
    }

    /// @brief Return fixed capacity of this ring buffer.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

private:
    static constexpr std::size_t Mask = Capacity - 1;

    alignas(CORIUM_CACHE_LINE_SIZE) std::array<Cell, Capacity> _buffer;
    alignas(CORIUM_CACHE_LINE_SIZE) std::atomic<std::size_t> _enqueuePos;
    alignas(CORIUM_CACHE_LINE_SIZE) std::atomic<std::size_t> _dequeuePos;
};

namespace internal {
    template <typename T, std::size_t Capacity>
    using MpscRingBuffer = ::corium::MpscRingBuffer<T, Capacity>;
} // namespace internal

} // namespace corium

// <<< End: corium/MpscRingBuffer.hpp

namespace corium {

/// @brief Event priority levels for multi-priority queue policies.
enum class EventPriority : uint8_t {
    High = 0,
    Normal = 1,
    Low = 2
};

/// @ingroup policies
/// @brief Queue Policy for fixed-capacity, zero-allocation lock-free MPSC RingBuffer.
/// @tparam EventVariant The variant type list of supported events.
/// @tparam Capacity Ring buffer capacity (must be a power of 2).
template <typename EventVariant = DefaultEvents, size_t Capacity = 1024>
class BoundedMpscQueuePolicy {
public:
    using EventType = EventVariant;
    static constexpr std::size_t capacity = Capacity; ///< Accessible queue capacity for profiler wiring.

    struct PushResult {
        bool pushed;
        bool wasEmpty;
    };

    /// @brief Try to push an event into the lock-free queue (thread-safe, zero allocations).
    PushResult tryPush(EventVariant&& event, EventPriority priority = EventPriority::Normal)
    {
        (void)priority;
        auto res = _ringBuffer.tryPush(std::move(event));
        return {res.pushed, res.wasEmpty};
    }

    /// @brief Try to push an event into the lock-free queue (const lvalue overload).
    PushResult tryPush(const EventVariant& event, EventPriority priority = EventPriority::Normal)
    {
        EventVariant copy = event;
        return tryPush(std::move(copy), priority);
    }

    /// @brief Try to pop an event from the lock-free queue (single consumer).
    bool tryPop(EventVariant& event)
    {
        return _ringBuffer.tryPop(event);
    }

    /// @brief Check if queue is empty.
    [[nodiscard]] bool empty() const noexcept
    {
        return _ringBuffer.empty();
    }

private:
    MpscRingBuffer<EventVariant, Capacity> _ringBuffer;
};

/// @brief Queue Policy supporting strict event priorities using separate lock-free MPSC ring buffers.
/// High priority events are always popped and dispatched before Normal/Low priority events.
/// @tparam EventVariant The variant type list of supported events.
/// @tparam HighCapacity Capacity for high-priority ring buffer (power of 2).
/// @tparam NormalCapacity Capacity for normal-priority ring buffer (power of 2).
/// @tparam LowCapacity Capacity for low-priority ring buffer (power of 2, 0 to disable).
template <
    typename EventVariant = DefaultEvents,
    size_t HighCapacity = 256,
    size_t NormalCapacity = 1024,
    size_t LowCapacity = 0
>
class PriorityMpscQueuePolicy {
public:
    using EventType = EventVariant;
    /// @brief Total accessible capacity (high + normal; low is optional).
    static constexpr std::size_t capacity = HighCapacity + NormalCapacity + (LowCapacity > 0 ? LowCapacity : 0);

    struct PushResult {
        bool pushed;
        bool wasEmpty;
    };

    /// @brief Try to push an event into the priority queue (thread-safe, zero dynamic allocation).
    PushResult tryPush(EventVariant&& event, EventPriority priority = EventPriority::Normal)
    {
        bool wasOverallEmpty = empty();
        bool pushed = false;

        switch (priority) {
            case EventPriority::High: {
                auto res = _highRingBuffer.tryPush(std::move(event));
                pushed = res.pushed;
                break;
            }
            case EventPriority::Normal: {
                auto res = _normalRingBuffer.tryPush(std::move(event));
                pushed = res.pushed;
                break;
            }
            case EventPriority::Low: {
                if constexpr (LowCapacity > 0) {
                    auto res = _lowRingBuffer.tryPush(std::move(event));
                    pushed = res.pushed;
                } else {
                    auto res = _normalRingBuffer.tryPush(std::move(event));
                    pushed = res.pushed;
                }
                break;
            }
        }

        return {pushed, wasOverallEmpty};
    }

    /// @brief Try to push an event into the priority queue (const lvalue overload).
    PushResult tryPush(const EventVariant& event, EventPriority priority = EventPriority::Normal)
    {
        EventVariant copy = event;
        return tryPush(std::move(copy), priority);
    }

    /// @brief Try to pop an event from the priority queue (strict priority order: High -> Normal -> Low).
    bool tryPop(EventVariant& event)
    {
        if (_highRingBuffer.tryPop(event)) {
            return true;
        }
        if (_normalRingBuffer.tryPop(event)) {
            return true;
        }
        if constexpr (LowCapacity > 0) {
            if (_lowRingBuffer.tryPop(event)) {
                return true;
            }
        }
        return false;
    }

    /// @brief Check if all priority queues are empty.
    [[nodiscard]] bool empty() const noexcept
    {
        bool isEmpty = _highRingBuffer.empty() && _normalRingBuffer.empty();
        if constexpr (LowCapacity > 0) {
            isEmpty = isEmpty && _lowRingBuffer.empty();
        }
        return isEmpty;
    }

private:
    MpscRingBuffer<EventVariant, HighCapacity> _highRingBuffer;
    MpscRingBuffer<EventVariant, NormalCapacity> _normalRingBuffer;
    [[no_unique_address]] std::conditional_t<(LowCapacity > 0),
        MpscRingBuffer<EventVariant, (LowCapacity > 0 ? LowCapacity : 1)>,
        std::monostate
    > _lowRingBuffer{};
};

/// @brief Queue Policy for a traditional mutex-protected blocking queue (power saving).
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
class BlockingQueuePolicy {
public:
    using EventType = EventVariant;

    struct PushResult {
        bool pushed;
        bool wasEmpty;
    };

    /// @brief Try to push an event into the blocking queue.
    PushResult tryPush(EventVariant event, EventPriority priority = EventPriority::Normal)
    {
        (void)priority;
#if CORIUM_HAS_STD_MUTEX
        std::lock_guard<std::mutex> lock(_mutex);
#endif
        bool wasEmpty = _queue.empty();
        _queue.push(std::move(event));
        return {true, wasEmpty};
    }

    /// @brief Try to pop an event from the blocking queue.
    bool tryPop(EventVariant& event)
    {
#if CORIUM_HAS_STD_MUTEX
        std::lock_guard<std::mutex> lock(_mutex);
#endif
        if (_queue.empty()) {
            return false;
        }
        event = std::move(_queue.front());
        _queue.pop();
        return true;
    }

    /// @brief Check if queue is empty.
    [[nodiscard]] bool empty() const
    {
#if CORIUM_HAS_STD_MUTEX
        std::lock_guard<std::mutex> lock(_mutex);
#endif
        return _queue.empty();
    }

private:
    std::queue<EventVariant> _queue;
#if CORIUM_HAS_STD_MUTEX
    mutable std::mutex _mutex;
#endif
};

/// @brief Zero-overhead Queue Policy for services or buses that do not receive or queue incoming events.
/// Occupies zero storage space when combined with [[no_unique_address]] in container types.
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
class NoQueuePolicy {
public:
    using EventType = EventVariant;

    struct PushResult {
        bool pushed = false;
        bool wasEmpty = false;
    };

    /// @brief Always fails to push events as queue capacity is 0.
    PushResult tryPush(EventVariant, EventPriority = EventPriority::Normal) noexcept
    {
        return {false, false};
    }

    /// @brief Always fails to pop events as queue capacity is 0.
    bool tryPop(EventVariant&) noexcept
    {
        return false;
    }

    /// @brief Always returns true as no events can be queued.
    [[nodiscard]] bool empty() const noexcept
    {
        return true;
    }
};

} // namespace corium


// <<< End: corium/policies/QueuePolicies.hpp

namespace corium {

/// @ingroup core
/// @brief Non-virtual type-erased event sink handle (raw pointer + static function pointer).
/// Zero dynamic heap allocations, zero vtables/RTTI.
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
class BasicEventSink {
    using PostFn = void (*)(void* sinkPtr, EventVariant&& event, EventPriority priority);

public:
    using EventVariantType = EventVariant;

    BasicEventSink() = default;

    BasicEventSink(const BasicEventSink&) = default;
    BasicEventSink& operator=(const BasicEventSink&) = default;
    BasicEventSink(BasicEventSink&&) noexcept = default;
    BasicEventSink& operator=(BasicEventSink&&) noexcept = default;

    template <typename ConcreteSink>
        requires (!std::is_same_v<std::decay_t<ConcreteSink>, BasicEventSink>)
    explicit BasicEventSink(ConcreteSink& sink)
        : _sinkPtr(&sink),
          _postFn([](void* ptr, EventVariant&& evt, EventPriority prio) {
              static_cast<ConcreteSink*>(ptr)->post(std::move(evt), prio);
          })
    {}

    /// @brief Post an event into the event sink with priority (rvalue overload).
    void post(EventVariant&& event, EventPriority priority = EventPriority::Normal) const
    {
        if (_postFn && _sinkPtr) {
            _postFn(_sinkPtr, std::move(event), priority);
        }
    }

    /// @brief Post an event into the event sink with priority (const lvalue overload).
    void post(const EventVariant& event, EventPriority priority = EventPriority::Normal) const
    {
        if (_postFn && _sinkPtr) {
            EventVariant copy = event;
            _postFn(_sinkPtr, std::move(copy), priority);
        }
    }

    /// @brief Convenience helper for posting high-priority events (e.g. from ISR handlers).
    template <typename Event>
    void postHighPriority(Event&& event) const
    {
        post(EventVariant(std::forward<Event>(event)), EventPriority::High);
    }

    explicit operator bool() const noexcept
    {
        return _sinkPtr != nullptr && _postFn != nullptr;
    }

private:
    void* _sinkPtr = nullptr;
    PostFn _postFn = nullptr;
};

/// @brief Default EventSink alias using DefaultEvents.
using EventSink = BasicEventSink<DefaultEvents>;

/// @brief Templated EventSink alias for custom event variant list.
template <typename EventVariant = DefaultEvents>
using EventSinkT = BasicEventSink<EventVariant>;

} // namespace corium

// <<< End: corium/EventSink.hpp

// >>> Begin: corium/internal/CallableTraits.hpp
/**
 * @file CallableTraits.hpp
 * @ingroup core
 * @brief Compile-time introspection traits for callable objects and event handlers.
 */


#include <type_traits>

namespace corium {
namespace internal {

template <typename T>
struct callable_event_type;

// Const member function operator() (const lambda)
template <typename C, typename R, typename EventType>
struct callable_event_type<R(C::*)(const EventType&) const> {
    using type = std::decay_t<EventType>;
};

// Non-const member function operator() (mutable lambda)
template <typename C, typename R, typename EventType>
struct callable_event_type<R(C::*)(const EventType&)> {
    using type = std::decay_t<EventType>;
};

// Value parameter const operator()
template <typename C, typename R, typename EventType>
struct callable_event_type<R(C::*)(EventType) const> {
    using type = std::decay_t<EventType>;
};

// Value parameter non-const operator()
template <typename C, typename R, typename EventType>
struct callable_event_type<R(C::*)(EventType)> {
    using type = std::decay_t<EventType>;
};

// Free function pointer (const ref)
template <typename R, typename EventType>
struct callable_event_type<R(*)(const EventType&)> {
    using type = std::decay_t<EventType>;
};

// Free function pointer (value)
template <typename R, typename EventType>
struct callable_event_type<R(*)(EventType)> {
    using type = std::decay_t<EventType>;
};

template <typename Callable, typename = void>
struct get_callable_event_type;

template <typename Callable>
struct get_callable_event_type<Callable, std::void_t<decltype(&std::decay_t<Callable>::operator())>> {
    using type = typename callable_event_type<decltype(&std::decay_t<Callable>::operator())>::type;
};

template <typename R, typename EventType>
struct get_callable_event_type<R(*)(const EventType&)> {
    using type = std::decay_t<EventType>;
};

template <typename R, typename EventType>
struct get_callable_event_type<R(*)(EventType)> {
    using type = std::decay_t<EventType>;
};

/// @brief Helper trait to deduce the concrete EventType argument from a callable (lambda, function pointer, or functor).
template <typename Callable>
using callable_event_type_t = typename get_callable_event_type<Callable>::type;

} // namespace internal

using internal::callable_event_type_t;

} // namespace corium

// <<< End: corium/internal/CallableTraits.hpp

// >>> Begin: corium/internal/FastDelegate.hpp
/**
 * @file FastDelegate.hpp
 * @ingroup core
 * @brief Zero-allocating Small Buffer Optimized (SBO) static delegate dispatcher.
 */


#include <cassert>
#include <cstddef>
#include <new> // IWYU pragma: keep
#include <type_traits>
#include <utility>

namespace corium {
namespace internal {

/// @brief Lightweight non-allocating delegate wrapper with 32-byte Small Buffer Optimization (SBO).
/// Eliminates std::function heap allocations and virtual table overhead for event handlers.
/// @tparam EventType The concrete event type invoked by this delegate.
template <typename EventType, std::size_t InlineSize = 32>
class EventHandlerDelegate {
    using StubFn = void (*)(void* instance, const EventType& event);
    using DestroyFn = void (*)(void* instance) noexcept;
    using MoveFn = void (*)(void* destStorage, void*& destInstance, void*& srcInstance) noexcept;

public:
    EventHandlerDelegate() noexcept = default;

    /// @brief Construct delegate wrapping a callable object (lambda, function pointer, or functor).
    /// @tparam Handler Callable type.
    /// @param handler Callable instance.
    template <
        typename Handler,
        typename Decayed = std::decay_t<Handler>,
        typename = std::enable_if_t<
            !std::is_same_v<Decayed, EventHandlerDelegate> &&
            std::is_invocable_r_v<void, Decayed&, const EventType&>
        >
    >
    explicit EventHandlerDelegate(Handler&& handler)
    {
        static_assert(
            std::is_nothrow_destructible_v<Decayed>,
            "Handler destructor must be noexcept"
        );
        static_assert(
            sizeof(Decayed) <= InlineSize,
            "Handler size exceeds FastDelegate inline storage size! Reduce captured state or increase InlineSize."
        );
        static_assert(
            alignof(Decayed) <= alignof(std::max_align_t),
            "Handler alignment requirement exceeds inline storage alignment."
        );
        static_assert(
            std::is_nothrow_move_constructible_v<Decayed>,
            "Handler must be nothrow move constructible."
        );

        void* storage = static_cast<void*>(_inlineStorage);
        ::new (storage) Decayed(std::forward<Handler>(handler));
        _instance = storage;

        _destroy = [](void* instance) noexcept {
            reinterpret_cast<Decayed*>(instance)->~Decayed();
        };

        _move = [](void* destStorage, void*& destInstance, void*& srcInstance) noexcept {
            auto* src = reinterpret_cast<Decayed*>(srcInstance);
            ::new (destStorage) Decayed(std::move(*src));
            src->~Decayed();
            destInstance = destStorage;
            srcInstance = nullptr;
        };

        _stub = [](void* instance, const EventType& event) {
            (*reinterpret_cast<Decayed*>(instance))(event);
        };
    }

    /// @brief Construct delegate from type-erased thunk operations.
    EventHandlerDelegate(
        void* srcObj,
        void (*stub)(void* instance, const EventType& event),
        void (*moveFn)(void* destStorage, void*& destInstance, void*& srcInstance) noexcept,
        void (*destroyFn)(void* instance) noexcept
    ) noexcept
    {
        void* storage = static_cast<void*>(_inlineStorage);
        _destroy = destroyFn;
        _move = moveFn;
        _stub = stub;
        if (_move) {
            _move(storage, _instance, srcObj);
        }
    }

    ~EventHandlerDelegate()
    {
        reset();
    }

    EventHandlerDelegate(EventHandlerDelegate&& rhs) noexcept
    {
        moveFrom(std::move(rhs));
    }

    EventHandlerDelegate& operator=(EventHandlerDelegate&& rhs) noexcept
    {
        if (this != &rhs) {
            reset();
            moveFrom(std::move(rhs));
        }

        return *this;
    }

    EventHandlerDelegate(const EventHandlerDelegate&) = delete;
    EventHandlerDelegate& operator=(const EventHandlerDelegate&) = delete;

    /// @brief Invoke wrapped handler directly with concrete event.
    /// @param event Event instance to pass to handler.
    void invoke(const EventType& event) const
    {
        assert(_stub && "Invoking an empty EventHandlerDelegate");

        if (!_stub) {
            return;
        }

        _stub(_instance, event);
    }

    /// @brief Function call operator for callable ergonomics.
    void operator()(const EventType& event) const
    {
        invoke(event);
    }

    /// @brief Check if delegate wraps a valid handler.
    explicit operator bool() const noexcept
    {
        return _stub != nullptr;
    }

    /// @brief Reset delegate to empty state.
    void reset() noexcept
    {
        if (_destroy) {
            _destroy(_instance);
        }

        _instance = nullptr;
        _stub = nullptr;
        _destroy = nullptr;
        _move = nullptr;
    }

private:
    void moveFrom(EventHandlerDelegate&& rhs) noexcept
    {
        _stub = rhs._stub;
        _destroy = rhs._destroy;
        _move = rhs._move;

        if (_move) {
            _move(
                static_cast<void*>(_inlineStorage),
                _instance,
                rhs._instance
            );
        } else {
            _instance = nullptr;
        }

        rhs._stub = nullptr;
        rhs._destroy = nullptr;
        rhs._move = nullptr;
    }

private:
    alignas(std::max_align_t) std::byte _inlineStorage[InlineSize > 0 ? InlineSize : 1]{};

    void* _instance = nullptr;
    StubFn _stub = nullptr;
    DestroyFn _destroy = nullptr;
    MoveFn _move = nullptr;
};

/// @brief Alias for EventHandlerDelegate.
template <typename EventType, std::size_t InlineSize = 32>
using FastDelegate = EventHandlerDelegate<EventType, InlineSize>;

} // namespace internal

using internal::EventHandlerDelegate;
using internal::FastDelegate;

} // namespace corium
// <<< End: corium/internal/FastDelegate.hpp

// >>> Begin: corium/internal/VariantIndex.hpp
/**
 * @file VariantIndex.hpp
 * @ingroup core
 * @brief Compile-time type index resolution for std::variant alternative types.
 */


#include <type_traits>
#include <variant>

namespace corium {
namespace internal {

template <typename T, typename... Types>
constexpr std::size_t get_variant_index_impl() {
    constexpr bool matches[] = { std::is_same_v<T, Types>... };
    
    for (std::size_t i = 0; i < sizeof...(Types); ++i) {
        if (matches[i]) return i;
    }
    
    return static_cast<std::size_t>(-1); 
}

/// @brief Helper trait to compute index of type T inside std::variant<Types...> at compile time.
template <typename T, typename Variant>
struct variant_index;

template <typename T, typename... Types>
struct variant_index<T, std::variant<Types...>> {
    static constexpr std::size_t value = get_variant_index_impl<T, Types...>();
    
    static_assert(value != static_cast<std::size_t>(-1), 
                  "ERROR: The requested Event type is not part of the std::variant!");
};

/// @brief Compile-time constant of type T's index in Variant.
template <typename T, typename Variant>
inline constexpr std::size_t variant_index_v = variant_index<T, Variant>::value;

/// @brief Helper trait to check if type T exists inside std::variant<Types...> at compile time.
template <typename T, typename Variant>
struct has_variant_type;

template <typename T, typename... Types>
struct has_variant_type<T, std::variant<Types...>> {
    static constexpr bool value = (std::is_same_v<T, Types> || ...);
};

/// @brief Compile-time boolean indicating if type T exists in Variant.
template <typename T, typename Variant>
inline constexpr bool has_variant_type_v = has_variant_type<T, Variant>::value;

template <typename T>
struct TypeId {
    static inline const char id = 0;
};

using TypeIdPtr = const void*;

/// @brief Get static unique address for type T without dynamic allocations or RTTI.
template <typename T>
inline TypeIdPtr getTypeId() noexcept {
    return &TypeId<T>::id;
}

template <typename T, typename = void>
struct extract_event_variant {
    using type = T;
};

template <typename T>
struct extract_event_variant<T, std::void_t<typename T::EventVariant>> {
    using type = typename T::EventVariant;
};

template <typename T>
using extract_event_variant_t = typename extract_event_variant<T>::type;

} // namespace internal

using internal::variant_index_v;
using internal::has_variant_type_v;
using internal::extract_event_variant_t;

} // namespace corium


// <<< End: corium/internal/VariantIndex.hpp

// >>> Begin: corium/policies/SignalPolicies.hpp
/**
 * @file SignalPolicies.hpp
 * @ingroup policies
 * @brief Thread wake-up policies (NoSignalPolicy, ConditionVariableSignalPolicy).
 */


#include <atomic>
#include <chrono>
#include <cstdint>

#if defined(_WIN32) || defined(_WIN64) || defined(__unix__) || defined(__APPLE__) || (defined(_GLIBCXX_HAS_GTHREADS) && _GLIBCXX_HAS_GTHREADS) || defined(_LIBCPP_HAS_THREAD_API_PTHREAD)
#include <condition_variable>
#include <mutex>
#include <thread>
#ifndef CORIUM_HAS_STD_MUTEX
#define CORIUM_HAS_STD_MUTEX 1
#endif
#else
#ifndef CORIUM_HAS_STD_MUTEX
#define CORIUM_HAS_STD_MUTEX 0
#endif
#endif

#ifdef __linux__
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#endif

namespace corium {

/// @brief Lightweight non-allocating static callback wrapper (function pointer + optional context argument).
struct StaticCallback {
    using SimpleFn = void (*)();
    using ContextFn = void (*)(void* arg);

    ContextFn fn = nullptr;
    void* arg = nullptr;

    StaticCallback() = default;

    /* implicit */ StaticCallback(SimpleFn simpleFn)
        : fn(reinterpret_cast<ContextFn>(simpleFn))
    {
        if (simpleFn) {
            // Helper trampoline for parameterless function pointers
            fn = [](void* context) {
                reinterpret_cast<SimpleFn>(context)();
            };
            arg = reinterpret_cast<void*>(simpleFn);
        }
    }

    StaticCallback(ContextFn contextFn, void* contextArg)
        : fn(contextFn), arg(contextArg)
    {}

    void operator()() const {
        if (fn) {
            fn(arg);
        }
    }

    explicit operator bool() const noexcept {
        return fn != nullptr;
    }
};

/// @brief Signal Policy for busy-spin / polling event loops (sub-microsecond latency, zero signaling cost).
class NoSignalPolicy {
public:
    void setOnQueueNonEmpty(StaticCallback cb) noexcept { (void)cb; }
    void signal() noexcept {}

    template <typename Rep, typename Period>
    void wait_for(const std::chrono::duration<Rep, Period>&) noexcept
    {
#if defined(__arm__) || defined(__aarch64__)
        asm volatile("yield");
#elif defined(__x86_64__)
        __builtin_ia32_pause();
#else
        // zero-op spin yield
#endif
    }
};

/// @brief Signal Policy invoking an edge-triggered callback on 0->1 transition when queue becomes non-empty.
class CallbackSignalPolicy {
public:
    void setOnQueueNonEmpty(StaticCallback callback)
    {
#if CORIUM_HAS_STD_MUTEX
        std::lock_guard<std::mutex> lock(_mutex);
#endif
        _callback = callback;
    }

    void signal()
    {
#if CORIUM_HAS_STD_MUTEX
        std::lock_guard<std::mutex> lock(_mutex);
        _hasEvents = true;
        _cv.notify_one();
#else
        _hasEvents = true;
#endif
        if (_callback) {
            _callback();
        }
    }

    template <typename Rep, typename Period>
    void wait_for(const std::chrono::duration<Rep, Period>& timeout)
    {
#if CORIUM_HAS_STD_MUTEX
        std::unique_lock<std::mutex> lock(_mutex);
        _cv.wait_for(lock, timeout, [this]() { return _hasEvents; });
        _hasEvents = false;
#else
        (void)timeout;
#if defined(__arm__) || defined(__aarch64__)
        asm volatile("yield");
#endif
        _hasEvents = false;
#endif
    }

private:
    StaticCallback _callback;
#if CORIUM_HAS_STD_MUTEX
    std::mutex _mutex;
    std::condition_variable _cv;
#endif
    bool _hasEvents = false;
};

/// @brief Signal Policy using C++20 std::atomic::wait() / notify_one() for zero-mutex futex signaling.
class AtomicWaitSignalPolicy {
public:
    void setOnQueueNonEmpty(StaticCallback callback)
    {
        _userCallback = callback;
    }

    void signal()
    {
        _flag.store(true, std::memory_order_release);
#if defined(__cpp_lib_atomic_wait) && __cpp_lib_atomic_wait >= 201907L
        _flag.notify_one();
#endif
        if (_userCallback) {
            _userCallback();
        }
    }

    template <typename Rep, typename Period>
    void wait_for(const std::chrono::duration<Rep, Period>& timeout)
    {
        if (_flag.load(std::memory_order_acquire)) {
            _flag.store(false, std::memory_order_relaxed);
            return;
        }
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!_flag.load(std::memory_order_acquire)) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return;
            }
#if CORIUM_HAS_STD_MUTEX
            std::this_thread::yield();
#elif defined(__arm__) || defined(__aarch64__)
            asm volatile("yield");
#endif
        }
        _flag.store(false, std::memory_order_relaxed);
    }

private:
    std::atomic<bool> _flag{false};
    StaticCallback _userCallback;
};

#ifdef __linux__
/// @brief Signal Policy using Linux eventfd for native epoll event loop integration.
class EventFdSignalPolicy {
public:
    EventFdSignalPolicy()
    {
        _fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    }

    ~EventFdSignalPolicy()
    {
        if (_fd >= 0) {
            ::close(_fd);
        }
    }

    EventFdSignalPolicy(const EventFdSignalPolicy&) = delete;
    EventFdSignalPolicy& operator=(const EventFdSignalPolicy&) = delete;

    EventFdSignalPolicy(EventFdSignalPolicy&& rhs) noexcept : _fd(rhs._fd)
    {
        rhs._fd = -1;
    }

    EventFdSignalPolicy& operator=(EventFdSignalPolicy&& rhs) noexcept
    {
        if (this != &rhs) {
            if (_fd >= 0) ::close(_fd);
            _fd = rhs._fd;
            rhs._fd = -1;
        }
        return *this;
    }

    void setOnQueueNonEmpty(StaticCallback callback)
    {
        _userCallback = callback;
    }

    void signal()
    {
        uint64_t val = 1;
        [[maybe_unused]] auto res = ::write(_fd, &val, sizeof(val));
        if (_userCallback) {
            _userCallback();
        }
    }

    template <typename Rep, typename Period>
    void wait_for(const std::chrono::duration<Rep, Period>& timeout)
    {
        if (_fd < 0) return;
        struct pollfd pfd;
        pfd.fd = _fd;
        pfd.events = POLLIN;
        int timeoutMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
        int res = ::poll(&pfd, 1, timeoutMs);
        if (res > 0 && (pfd.revents & POLLIN)) {
            uint64_t val = 0;
            [[maybe_unused]] auto bytes = ::read(_fd, &val, sizeof(val));
        }
    }

    [[nodiscard]] int nativeHandle() const noexcept
    {
        return _fd;
    }

private:
    int _fd = -1;
    StaticCallback _userCallback;
};
#endif // __linux__

} // namespace corium

// <<< End: corium/policies/SignalPolicies.hpp

// >>> Begin: corium/timers/TimerScheduler.hpp
/**
 * @file TimerScheduler.hpp
 * @ingroup timers
 * @brief Fixed-capacity static timer scheduler for delayed and periodic events.
 */


#include <chrono>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>


// >>> Begin: corium/internal/StaticMinHeap.hpp
/**
 * @file StaticMinHeap.hpp
 * @ingroup internal
 * @brief Fixed-capacity zero-heap binary min-heap for deterministic priority queues and timers.
 */


#include <array>
#include <cassert>
#include <cstddef>
#include <functional>
#include <utility>

namespace corium::internal {

/// @brief Fixed-capacity statically-allocated binary min-heap.
/// Zero dynamic heap allocations.
/// @tparam T Element type stored in the heap.
/// @tparam Capacity Maximum number of elements.
/// @tparam Compare Comparator predicate (std::greater<T> for min-heap where smallest element is at top).
template <
    typename T,
    std::size_t Capacity,
    typename Compare = std::greater<T>
>
class StaticMinHeap {
public:
    constexpr StaticMinHeap() = default;

    /// @brief Push an element into the heap.
    /// @return true if pushed, false if capacity exceeded.
    template <typename U>
    bool push(U&& value) {
        if (_size >= Capacity) {
            return false;
        }

        _data[_size] = std::forward<U>(value);
        siftUp(_size);
        _size++;
        return true;
    }

    /// @brief Remove the top element from the heap.
    /// @return true if popped, false if heap was empty.
    bool pop() noexcept {
        if (_size == 0) {
            return false;
        }

        _size--;
        if (_size > 0) {
            _data[0] = std::move(_data[_size]);
            siftDown(0);
        }
        return true;
    }

    /// @brief Access top element.
    [[nodiscard]] const T& top() const noexcept {
        assert(_size > 0 && "Accessing top of empty StaticMinHeap");
        return _data[0];
    }

    /// @brief Access mutable top element.
    [[nodiscard]] T& top() noexcept {
        assert(_size > 0 && "Accessing top of empty StaticMinHeap");
        return _data[0];
    }

    /// @brief Current number of elements in the heap.
    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return _size;
    }

    /// @brief Maximum capacity of the heap.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

    /// @brief Check if heap is empty.
    [[nodiscard]] constexpr bool empty() const noexcept {
        return _size == 0;
    }

    /// @brief Check if heap is full.
    [[nodiscard]] constexpr bool full() const noexcept {
        return _size >= Capacity;
    }

    /// @brief Clear all elements.
    void clear() noexcept {
        _size = 0;
    }

    /// @brief Direct access to underlying data array.
    [[nodiscard]] T* data() noexcept {
        return _data.data();
    }

    /// @brief Direct const access to underlying data array.
    [[nodiscard]] const T* data() const noexcept {
        return _data.data();
    }

    /// @brief Sift down an element at specified index (e.g. after in-place modification).
    void siftDown(std::size_t index) noexcept {
        Compare comp;
        std::size_t current = index;

        while (true) {
            std::size_t left = 2 * current + 1;
            std::size_t right = 2 * current + 2;
            std::size_t smallest = current;

            if (left < _size && comp(_data[smallest], _data[left])) {
                smallest = left;
            }
            if (right < _size && comp(_data[smallest], _data[right])) {
                smallest = right;
            }

            if (smallest != current) {
                using std::swap;
                swap(_data[current], _data[smallest]);
                current = smallest;
            } else {
                break;
            }
        }
    }

    /// @brief Sift up an element at specified index.
    void siftUp(std::size_t index) noexcept {
        Compare comp;
        std::size_t current = index;

        while (current > 0) {
            std::size_t parent = (current - 1) / 2;
            if (comp(_data[parent], _data[current])) {
                using std::swap;
                swap(_data[parent], _data[current]);
                current = parent;
            } else {
                break;
            }
        }
    }

private:
    std::array<T, Capacity> _data{};
    std::size_t _size{0};
};

} // namespace corium::internal

// <<< End: corium/internal/StaticMinHeap.hpp

// >>> Begin: corium/timers/ClockPolicies.hpp
/**
 * @file ClockPolicies.hpp
 * @ingroup timers
 * @brief Hardware and simulated clock policies (Chrono, Manual, Tick, EspTimer, FreeRTOS).
 */


#include <chrono>
#include <cstdint>
#include <type_traits>

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <esp_timer.h>
#define CORIUM_NATIVE_ESP_TIMER 1
#endif

#if defined(FREERTOS) || defined(INC_FREERTOS_H)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#define CORIUM_NATIVE_FREERTOS 1
#endif

namespace corium {

class ChronoClockPolicy;

namespace internal {

template <typename T, typename = void>
struct get_timer_clock_policy {
    using type = ChronoClockPolicy;
};

template <typename T>
struct get_timer_clock_policy<T, std::void_t<typename T::clock_policy>> {
    using type = typename T::clock_policy;
};

} // namespace internal

/// @ingroup timers
/// @brief Default host clock policy using std::chrono::steady_clock.
class ChronoClockPolicy {
public:
    using time_point = std::chrono::steady_clock::time_point;
    using duration = std::chrono::microseconds;

    [[nodiscard]] static time_point now() noexcept
    {
        return std::chrono::steady_clock::now();
    }

    template <typename Rep, typename Period>
    [[nodiscard]] static time_point add(time_point tp, const std::chrono::duration<Rep, Period>& d) noexcept
    {
        return tp + std::chrono::duration_cast<std::chrono::steady_clock::duration>(d);
    }

    [[nodiscard]] static bool isDue(time_point now, time_point expiry) noexcept
    {
        return now >= expiry;
    }
};

/// @ingroup timers
/// @brief Deterministic, manually-advanced clock policy for unit tests and simulation.
class ManualClockPolicy {
public:
    using time_point = uint64_t;
    using duration = uint64_t;

    [[nodiscard]] static time_point now() noexcept
    {
        return _currentTime;
    }

    static void set(time_point t) noexcept
    {
        _currentTime = t;
    }

    static void advance(duration delta) noexcept
    {
        _currentTime += delta;
    }

    static void reset() noexcept
    {
        _currentTime = 0;
    }

    template <typename Rep, typename Period>
    [[nodiscard]] static time_point add(time_point tp, const std::chrono::duration<Rep, Period>& d) noexcept
    {
        return tp + static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(d).count());
    }

    [[nodiscard]] static time_point add(time_point tp, duration delta) noexcept
    {
        return tp + delta;
    }

    [[nodiscard]] static bool isDue(time_point now, time_point expiry) noexcept
    {
        return now >= expiry;
    }

private:
    static inline time_point _currentTime = 0;
};

/// @brief Clock policy driven by a custom microsecond tick provider.
/// @tparam Provider Struct with static `uint64_t nowUs()` method.
template <typename Provider>
class MicrosecondTickClockPolicy {
public:
    using time_point = uint64_t;
    using duration = uint64_t;

    [[nodiscard]] static time_point now() noexcept
    {
        return Provider::nowUs();
    }

    template <typename Rep, typename Period>
    [[nodiscard]] static time_point add(time_point tp, const std::chrono::duration<Rep, Period>& d) noexcept
    {
        return tp + static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(d).count());
    }

    [[nodiscard]] static time_point add(time_point tp, duration delta) noexcept
    {
        return tp + delta;
    }

    [[nodiscard]] static bool isDue(time_point now, time_point expiry) noexcept
    {
        return now >= expiry;
    }
};

/// @brief Clock policy driven by a custom millisecond tick provider (e.g. millis(), HAL_GetTick()).
/// @tparam Provider Struct with static `uint32_t nowMs()` method.
template <typename Provider>
class MillisecondTickClockPolicy {
public:
    using time_point = uint32_t;
    using duration = uint32_t;

    [[nodiscard]] static time_point now() noexcept
    {
        return Provider::nowMs();
    }

    template <typename Rep, typename Period>
    [[nodiscard]] static time_point add(time_point tp, const std::chrono::duration<Rep, Period>& d) noexcept
    {
        return tp + static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
    }

    [[nodiscard]] static time_point add(time_point tp, duration delta) noexcept
    {
        return tp + delta;
    }

    [[nodiscard]] static bool isDue(time_point now, time_point expiry) noexcept
    {
        return now >= expiry;
    }
};

/// @brief Native ESP32 Hardware Timer clock policy using `esp_timer_get_time()` (64-bit microsecond counter).
class EspTimerClockPolicy {
public:
    using time_point = uint64_t;
    using duration = uint64_t;

    [[nodiscard]] static time_point now() noexcept
    {
#if defined(CORIUM_NATIVE_ESP_TIMER)
        int64_t t = esp_timer_get_time();
        return t > 0 ? static_cast<uint64_t>(t) : 0;
#else
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
#endif
    }

    template <typename Rep, typename Period>
    [[nodiscard]] static time_point add(time_point tp, const std::chrono::duration<Rep, Period>& d) noexcept
    {
        return tp + static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(d).count());
    }

    [[nodiscard]] static time_point add(time_point tp, duration delta) noexcept
    {
        return tp + delta;
    }

    [[nodiscard]] static bool isDue(time_point now, time_point expiry) noexcept
    {
        return now >= expiry;
    }
};

/// @brief FreeRTOS RTOS Tick clock policy using `xTaskGetTickCount()`.
class FreeRtosClockPolicy {
public:
    using time_point = uint32_t;
    using duration = uint32_t;

    [[nodiscard]] static time_point now() noexcept
    {
#if defined(CORIUM_NATIVE_FREERTOS)
        return static_cast<uint32_t>(xTaskGetTickCount());
#else
        return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
#endif
    }

    template <typename Rep, typename Period>
    [[nodiscard]] static time_point add(time_point tp, const std::chrono::duration<Rep, Period>& d) noexcept
    {
        return tp + static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
    }

    [[nodiscard]] static time_point add(time_point tp, duration delta) noexcept
    {
        return tp + delta;
    }

    [[nodiscard]] static bool isDue(time_point now, time_point expiry) noexcept
    {
        return now >= expiry;
    }
};

} // namespace corium

// <<< End: corium/timers/ClockPolicies.hpp

namespace corium {

using TimerId = uint32_t;
constexpr TimerId INVALID_TIMER_ID = 0;

/// @ingroup timers
/// @brief Zero-heap Min-Heap Timer Scheduler for delayed and periodic events.
/// Provides O(1) earliest-due timer checks and O(log N) insertion and rescheduling.
/// Supports single-shot delayed events and recurring periodic events with compile-time clock policy.
/// @tparam EventVariant Supported event variant list type.
/// @tparam MaxTimers Maximum number of concurrent active timers (static min-heap capacity).
/// @tparam ClockPolicy Policy governing time acquisition and arithmetic (ChronoClockPolicy default).
template <
    typename EventVariant = DefaultEvents,
    size_t MaxTimers = 64,
    typename ClockPolicy = ChronoClockPolicy
>
class TimerScheduler {
public:
    using Clock = ClockPolicy;
    using time_point = typename ClockPolicy::time_point;
    using duration = typename ClockPolicy::duration;

    struct TimerEntry {
        TimerId id = INVALID_TIMER_ID;
        EventVariant event{};
        time_point expiryTime{};
        duration interval{};
        EventPriority priority = EventPriority::Normal;
        bool isPeriodic = false;
        bool active = false;
    };

    struct TimerComparator {
        bool operator()(const TimerEntry& a, const TimerEntry& b) const noexcept {
            return a.expiryTime > b.expiryTime; // Min-heap: earliest expiry at top
        }
    };

    TimerScheduler() = default;

    /// @brief Schedule a single-shot delayed event with std::chrono duration.
    template <typename Rep, typename Period>
    TimerId scheduleDelayed(EventVariant event, const std::chrono::duration<Rep, Period>& delay, EventPriority priority = EventPriority::Normal)
    {
        time_point now = ClockPolicy::now();
        time_point expiry = ClockPolicy::add(now, delay);
        duration zeroInterval{};
        return allocateTimer(std::move(event), expiry, zeroInterval, priority, false);
    }

    /// @brief Schedule a single-shot delayed event with native clock duration.
    TimerId scheduleDelayed(EventVariant event, duration delay, EventPriority priority = EventPriority::Normal)
        requires (!std::is_same_v<duration, std::chrono::microseconds> && !std::is_same_v<duration, std::chrono::milliseconds>)
    {
        time_point now = ClockPolicy::now();
        time_point expiry = ClockPolicy::add(now, delay);
        duration zeroInterval{};
        return allocateTimer(std::move(event), expiry, zeroInterval, priority, false);
    }

    /// @brief Schedule a recurring periodic event with std::chrono duration.
    template <typename Rep, typename Period>
    TimerId schedulePeriodic(EventVariant event, const std::chrono::duration<Rep, Period>& interval, EventPriority priority = EventPriority::Normal)
    {
        time_point now = ClockPolicy::now();
        time_point expiry = ClockPolicy::add(now, interval);
        if constexpr (std::is_same_v<duration, std::chrono::microseconds>) {
            return allocateTimer(std::move(event), expiry, std::chrono::duration_cast<std::chrono::microseconds>(interval), priority, true);
        } else if constexpr (std::is_same_v<duration, std::chrono::milliseconds>) {
            return allocateTimer(std::move(event), expiry, std::chrono::duration_cast<std::chrono::milliseconds>(interval), priority, true);
        } else {
            auto nativeInterval = static_cast<duration>(std::chrono::duration_cast<std::chrono::microseconds>(interval).count());
            return allocateTimer(std::move(event), expiry, nativeInterval, priority, true);
        }
    }

    /// @brief Schedule a recurring periodic event with native clock duration.
    TimerId schedulePeriodic(EventVariant event, duration interval, EventPriority priority = EventPriority::Normal)
        requires (!std::is_same_v<duration, std::chrono::microseconds> && !std::is_same_v<duration, std::chrono::milliseconds>)
    {
        time_point now = ClockPolicy::now();
        time_point expiry = ClockPolicy::add(now, interval);
        return allocateTimer(std::move(event), expiry, interval, priority, true);
    }

    /// @brief Cancel an active timer by its TimerId handle.
    /// @param id Handle of the timer to cancel.
    /// @return true if timer was found and cancelled; false if handle was invalid/inactive.
    bool cancelTimer(TimerId id) noexcept
    {
        if (id == INVALID_TIMER_ID || _activeCount == 0) {
            return false;
        }

        auto* data = _heap.data();
        const std::size_t n = _heap.size();
        for (std::size_t i = 0; i < n; ++i) {
            if (data[i].active && data[i].id == id) {
                data[i].active = false;
                data[i].id = INVALID_TIMER_ID;
                if (_activeCount > 0) {
                    _activeCount--;
                }
                return true;
            }
        }
        return false;
    }

    /// @brief Process all due timers and post their events into target event bus or sink.
    /// Uses O(1) early exit when the earliest timer is not yet due.
    /// @tparam TargetEventSink Target event sink type (e.g. BasicEventBus or EventSink).
    /// @param sink Target sink receiving due timer events.
    /// @param now Current time point (defaults to ClockPolicy::now()).
    /// @return Number of events posted during this check.
    template <typename EventSink>
    std::size_t processDueTimers(EventSink& sink, time_point now = ClockPolicy::now())
    {
        if (_activeCount == 0 || _heap.empty()) {
            return 0;
        }

        std::size_t posted = 0;

        while (!_heap.empty()) {
            auto& top = _heap.top();

            // Discard cancelled timers at top of heap
            if (!top.active) {
                _heap.pop();
                continue;
            }

            // O(1) early exit: if earliest scheduled timer is not due, nothing else is due
            if (!ClockPolicy::isDue(now, top.expiryTime)) {
                break;
            }

            TimerEntry entry = std::move(_heap.top());
            _heap.pop();

            sink.post(entry.event, entry.priority);
            posted++;

            if (entry.isPeriodic) {
                entry.expiryTime = ClockPolicy::add(entry.expiryTime, entry.interval);
                while (ClockPolicy::isDue(now, entry.expiryTime)) {
                    entry.expiryTime = ClockPolicy::add(entry.expiryTime, entry.interval);
                }
                _heap.push(std::move(entry));
            } else {
                if (_activeCount > 0) {
                    _activeCount--;
                }
            }
        }

        return posted;
    }

    /// @brief Get current number of active timers.
    [[nodiscard]] size_t activeCount() const noexcept
    {
        return _activeCount;
    }

    /// @brief Get maximum timer capacity.
    [[nodiscard]] constexpr size_t capacity() const noexcept
    {
        return MaxTimers;
    }

private:
    TimerId allocateTimer(EventVariant event, time_point expiryTime, duration interval, EventPriority priority, bool isPeriodic)
    {
        if (_activeCount >= MaxTimers || _heap.full()) {
            return INVALID_TIMER_ID;
        }

        TimerId id = _nextId++;
        if (_nextId == INVALID_TIMER_ID) {
            _nextId = 1;
        }

        TimerEntry entry{
            .id = id,
            .event = std::move(event),
            .expiryTime = expiryTime,
            .interval = interval,
            .priority = priority,
            .isPeriodic = isPeriodic,
            .active = true
        };

        if (_heap.push(std::move(entry))) {
            _activeCount++;
            return id;
        }

        return INVALID_TIMER_ID;
    }

    internal::StaticMinHeap<TimerEntry, MaxTimers, TimerComparator> _heap{};
    size_t _activeCount = 0;
    TimerId _nextId = 1;
};

} // namespace corium

// <<< End: corium/timers/TimerScheduler.hpp

namespace corium {

/// @ingroup core
/// @brief Context object passed to Application providing event registration, sink access, quit requests, and timer scheduling.
/// Completely encapsulates EventBus and Reactor internals.
/// @tparam EventVariant Supported event variant list type (defaults to DefaultEvents).
template <typename EventVariant = DefaultEvents>
class ApplicationContext {
    static constexpr std::size_t NumEvents = std::variant_size_v<EventVariant>;

    using RegFn = bool (*)(
        void* busPtr,
        void* handlerObj,
        void* invokerFn,
        void (*mover)(void* destStorage, void*& destInstance, void*& srcInstance) noexcept,
        void (*destroyer)(void* instance) noexcept,
        std::size_t size
    );

public:
    using EventVariantType = EventVariant;

    using ScheduleDelayedFn = TimerId (*)(void* ptr, EventVariant event, std::chrono::microseconds delay, EventPriority priority);
    using SchedulePeriodicFn = TimerId (*)(void* ptr, EventVariant event, std::chrono::microseconds interval, EventPriority priority);
    using CancelTimerFn = bool (*)(void* ptr, TimerId id);

    ApplicationContext() = default;

    template <typename EventBusType>
    ApplicationContext(EventBusType& events, StaticCallback quitCallback)
        : _busPtr(&events), _eventSink(events.sink()), _quitCallback(quitCallback)
    {
        initRegFns<EventBusType>(std::make_index_sequence<NumEvents>{});
    }

    template <typename Scheduler>
    void setTimerScheduler(Scheduler& scheduler) noexcept
    {
        _timerSchedulerPtr = &scheduler;
        _scheduleDelayedFn = [](void* ptr, EventVariant evt, std::chrono::microseconds delay, EventPriority prio) {
            return static_cast<Scheduler*>(ptr)->scheduleDelayed(std::move(evt), delay, prio);
        };
        _schedulePeriodicFn = [](void* ptr, EventVariant evt, std::chrono::microseconds interval, EventPriority prio) {
            return static_cast<Scheduler*>(ptr)->schedulePeriodic(std::move(evt), interval, prio);
        };
        _cancelTimerFn = [](void* ptr, TimerId id) {
            return static_cast<Scheduler*>(ptr)->cancelTimer(id);
        };
    }

    /// @brief Register an event handler into the application event bus.
    template <typename Handler>
    bool registerHandler(Handler&& handler)
    {
        using EventType = callable_event_type_t<Handler>;
        static_assert(has_variant_type_v<EventType, EventVariant>, "EventType is not part of Application's EventVariant list!");
        constexpr std::size_t typeIdx = variant_index_v<EventType, EventVariant>;

        if (_busPtr && _regFns[typeIdx]) {
            using Decayed = std::decay_t<Handler>;
            Decayed h(std::forward<Handler>(handler));

            void (*invoker)(void* instance, const EventType& event) = [](void* instance, const EventType& event) {
                (*static_cast<Decayed*>(instance))(event);
            };

            auto mover = [](void* destStorage, void*& destInstance, void*& srcInstance) noexcept {
                auto* src = static_cast<Decayed*>(srcInstance);
                ::new (destStorage) Decayed(std::move(*src));
                src->~Decayed();
                destInstance = destStorage;
                srcInstance = nullptr;
            };

            auto destroyer = [](void* instance) noexcept {
                static_cast<Decayed*>(instance)->~Decayed();
            };

            void* srcPtr = &h;
            return _regFns[typeIdx](_busPtr, srcPtr, reinterpret_cast<void*>(invoker), mover, destroyer, sizeof(Decayed));
        }
        return false;
    }

    /// @brief Register a filtered event handler executed only when predicate evaluates to true.
    /// @tparam Filter Callable returning bool when passed the event.
    /// @tparam Handler Callable accepting const EventType&.
    template <typename Filter, typename Handler>
    bool registerFilteredHandler(Filter&& filter, Handler&& handler)
    {
        using EventType = callable_event_type_t<Handler>;
        return registerHandler([f = std::forward<Filter>(filter), h = std::forward<Handler>(handler)](const EventType& event) {
            if (f(event)) {
                h(event);
            }
        });
    }

    /// @brief Access event sink handle.
    [[nodiscard]] EventSinkT<EventVariant> eventSink() const noexcept
    {
        return _eventSink;
    }

    /// @brief Schedule a single-shot delayed event with std::chrono duration.
    template <typename Rep, typename Period>
    [[nodiscard]] TimerId scheduleDelayed(EventVariant event, const std::chrono::duration<Rep, Period>& delay, EventPriority priority = EventPriority::Normal) const
    {
        if (_scheduleDelayedFn && _timerSchedulerPtr) {
            return _scheduleDelayedFn(_timerSchedulerPtr, std::move(event), std::chrono::duration_cast<std::chrono::microseconds>(delay), priority);
        }
        return INVALID_TIMER_ID;
    }

    /// @brief Schedule a recurring periodic event with std::chrono duration.
    template <typename Rep, typename Period>
    [[nodiscard]] TimerId schedulePeriodic(EventVariant event, const std::chrono::duration<Rep, Period>& interval, EventPriority priority = EventPriority::Normal) const
    {
        if (_schedulePeriodicFn && _timerSchedulerPtr) {
            return _schedulePeriodicFn(_timerSchedulerPtr, std::move(event), std::chrono::duration_cast<std::chrono::microseconds>(interval), priority);
        }
        return INVALID_TIMER_ID;
    }

    /// @brief Cancel an active timer.
    [[nodiscard]] bool cancelTimer(TimerId id) const noexcept
    {
        if (_cancelTimerFn && _timerSchedulerPtr) {
            return _cancelTimerFn(_timerSchedulerPtr, id);
        }
        return false;
    }

    /// @brief Request graceful application exit.
    void requestQuit() const
    {
        if (_quitCallback) {
            _quitCallback();
        }
    }

    /// @brief Attach runtime detachment handle.
    void setRuntimeDetach(void* runtimePtr, void (*detachFn)(void*) noexcept) noexcept
    {
        _runtimePtr = runtimePtr;
        _detachFn = detachFn;
    }

    /// @brief Detach application from runtime to prevent dangling callbacks on shutdown.
    void detachFromRuntime() noexcept
    {
        if (_detachFn && _runtimePtr) {
            _detachFn(_runtimePtr);
            _detachFn = nullptr;
            _runtimePtr = nullptr;
        }
    }

    /// @brief Reset context state to empty.
    void reset() noexcept
    {
        _busPtr = nullptr;
        _runtimePtr = nullptr;
        _detachFn = nullptr;
        _timerSchedulerPtr = nullptr;
        _scheduleDelayedFn = nullptr;
        _schedulePeriodicFn = nullptr;
        _cancelTimerFn = nullptr;
        _quitCallback = StaticCallback{};
    }

    explicit operator bool() const noexcept
    {
        return _busPtr != nullptr;
    }

private:
    template <typename EventBusType, std::size_t... Is>
    void initRegFns(std::index_sequence<Is...>) noexcept
    {
        ((_regFns[Is] = [](
            void* bPtr,
            void* handlerObj,
            void* invokerFn,
            void (*mover)(void* destStorage, void*& destInstance, void*& srcInstance) noexcept,
            void (*destroyer)(void* instance) noexcept,
            std::size_t size
        ) -> bool {
            using EventType = std::variant_alternative_t<Is, EventVariant>;
            using StoragePolicy = typename EventBusType::ReactorType::StoragePolicyType;
            constexpr std::size_t InlineSize = StoragePolicy::inline_storage_size;

            if (size > InlineSize) {
                return false;
            }

            auto* bus = static_cast<EventBusType*>(bPtr);
            auto stub = reinterpret_cast<void (*)(void*, const EventType&)>(invokerFn);

            EventHandlerDelegate<EventType, InlineSize> del(handlerObj, stub, mover, destroyer);
            return bus->template registerHandler<EventType>(std::move(del));
        }), ...);
    }

    void* _busPtr = nullptr;
    EventSinkT<EventVariant> _eventSink{};
    StaticCallback _quitCallback{};
    std::array<RegFn, NumEvents> _regFns{};

    void* _timerSchedulerPtr = nullptr;
    ScheduleDelayedFn _scheduleDelayedFn = nullptr;
    SchedulePeriodicFn _schedulePeriodicFn = nullptr;
    CancelTimerFn _cancelTimerFn = nullptr;

    void* _runtimePtr = nullptr;
    void (*_detachFn)(void*) noexcept = nullptr;
};

} // namespace corium

// <<< End: corium/ApplicationContext.hpp

// >>> Begin: corium/EventBus.hpp
/**
 * @file EventBus.hpp
 * @ingroup core
 * @brief Multi-producer single-consumer lock-free event bus coordinator.
 */



// >>> Begin: corium/internal/EventQueue.hpp
/**
 * @file EventQueue.hpp
 * @ingroup core
 * @brief Internal priority and bounded lock-free event queue adapter.
 */


#include <optional>
#include <utility>


// >>> Begin: corium/policies/OverflowPolicies.hpp
/**
 * @file OverflowPolicies.hpp
 * @ingroup policies
 * @brief Queue saturation policies (DropNewest, DropOldest, Audit, Panic).
 */


#include <atomic>
#include <cassert>
#include <cstdint>
#include <exception>
#include <utility>


namespace corium {

/// @brief Default Overflow Policy: Silently drop incoming new event when queue is full. Zero overhead.
struct DropNewestOverflowPolicy {
    template <typename QueuePolicy, typename EventVariant>
    bool handleOverflow(QueuePolicy& queue, EventVariant&& event, EventPriority priority = EventPriority::Normal)
    {
        (void)queue;
        (void)event;
        (void)priority;
        return false;
    }
};

/// @brief Overflow Policy: Evict oldest event in queue to make room for the new event.
/// Ideal for high-frequency telemetry and sensor data where newest reading is critical.
struct DropOldestOverflowPolicy {
    template <typename QueuePolicy, typename EventVariant>
    bool handleOverflow(QueuePolicy& queue, EventVariant&& event, EventPriority priority = EventPriority::Normal)
    {
        typename QueuePolicy::EventType dummy;
        queue.tryPop(dummy); // Evict oldest event
        auto res = queue.tryPush(std::forward<EventVariant>(event), priority);
        return res.pushed;
    }
};

/// @brief Overflow Policy: Trigger assertion / panic when queue fills up.
/// Essential for mission-critical systems where event loss is unacceptable.
struct PanicOverflowPolicy {
    template <typename QueuePolicy, typename EventVariant>
    bool handleOverflow(QueuePolicy& queue, EventVariant&& event, EventPriority priority = EventPriority::Normal)
    {
        (void)queue;
        (void)event;
        (void)priority;
#if defined(CORIUM_PANIC_ON_OVERFLOW)
        std::terminate();
#else
        assert(false && "Corium Queue Overflow: Queue capacity exceeded!");
        return false;
#endif
    }
};

/// @brief Overflow Policy: Audit and count dropped events via atomic counter.
class AuditOverflowPolicy {
public:
    template <typename QueuePolicy, typename EventVariant>
    bool handleOverflow(QueuePolicy& queue, EventVariant&& event, EventPriority priority = EventPriority::Normal)
    {
        (void)queue;
        (void)event;
        (void)priority;
        _droppedCount.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    /// @brief Get total number of dropped events.
    [[nodiscard]] uint64_t overflowCount() const noexcept
    {
        return _droppedCount.load(std::memory_order_relaxed);
    }

    /// @brief Reset total dropped event counter to 0.
    void resetOverflowCount() noexcept
    {
        _droppedCount.store(0, std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t> _droppedCount{0};
};

} // namespace corium

// <<< End: corium/policies/OverflowPolicies.hpp

namespace corium {
namespace internal {

/// @brief Event queue composing QueuePolicy, SignalPolicy, and OverflowPolicy strategy types.
/// @tparam QueuePolicy Queueing policy strategy.
/// @tparam SignalPolicy Signaling policy strategy.
/// @tparam OverflowPolicy Strategy for handling queue overflow when capacity is exceeded.
template <
    typename QueuePolicy = BoundedMpscQueuePolicy<DefaultEvents, 1024>,
    typename SignalPolicy = NoSignalPolicy,
    typename OverflowPolicy = DropNewestOverflowPolicy
>
class EventQueue {
public:
    using EventVariant = typename QueuePolicy::EventType;

    EventQueue() = default;

    /// @brief Push an event into the queue and trigger signal policy on 0->1 transition (rvalue overload).
    bool pushEvent(EventVariant&& event, EventPriority priority = EventPriority::Normal)
    {
        auto res = _queuePolicy.tryPush(event, priority);
        if (!res.pushed) {
            return _overflowPolicy.handleOverflow(_queuePolicy, std::move(event), priority);
        }

        if (res.wasEmpty) {
            _signalPolicy.signal();
        }

        return true;
    }

    /// @brief Push an event into the queue (const lvalue overload).
    bool pushEvent(const EventVariant& event, EventPriority priority = EventPriority::Normal)
    {
        EventVariant copy = event;
        return pushEvent(std::move(copy), priority);
    }

    /// @brief Pop an event directly into output reference without intermediate optional.
    bool tryPopEvent(EventVariant& event)
    {
        return _queuePolicy.tryPop(event);
    }

    /// @brief Pop an event from the queue.
    /// @return Optional containing popped event, or std::nullopt if empty.
    std::optional<EventVariant> tryPopEvent()
    {
        EventVariant event;
        if (_queuePolicy.tryPop(event)) {
            return event;
        }
        return std::nullopt;
    }

    /// @brief Check if event queue is empty.
    [[nodiscard]] bool empty() const
    {
        return _queuePolicy.empty();
    }

    /// @brief Set non-allocating static callback triggered when queue transitions from empty to non-empty.
    void setOnQueueNonEmpty(StaticCallback callback)
    {
        _signalPolicy.setOnQueueNonEmpty(callback);
    }

    /// @brief Access reference to signal policy.
    [[nodiscard]] SignalPolicy& signalPolicy() noexcept
    {
        return _signalPolicy;
    }

    /// @brief Access const reference to signal policy.
    [[nodiscard]] const SignalPolicy& signalPolicy() const noexcept
    {
        return _signalPolicy;
    }

    /// @brief Access reference to overflow policy.
    [[nodiscard]] OverflowPolicy& overflowPolicy() noexcept
    {
        return _overflowPolicy;
    }

    /// @brief Access const reference to overflow policy.
    [[nodiscard]] const OverflowPolicy& overflowPolicy() const noexcept
    {
        return _overflowPolicy;
    }

private:
    [[no_unique_address]] QueuePolicy _queuePolicy;
    [[no_unique_address]] SignalPolicy _signalPolicy;
    [[no_unique_address]] OverflowPolicy _overflowPolicy;
};

} // namespace internal

using internal::EventQueue;

} // namespace corium

// <<< End: corium/internal/EventQueue.hpp

// >>> Begin: corium/internal/Reactor.hpp
/**
 * @file Reactor.hpp
 * @ingroup core
 * @brief Internal static event handler registry and compile-time dispatcher.
 */


#include <array>
#include <cassert>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>


// >>> Begin: corium/internal/Panic.hpp
/**
 * @file Panic.hpp
 * @ingroup core
 * @brief Zero-heap assertion and panic handling utilities.
 */


#include <cstdio>
#include <cstdlib>

namespace corium {

/// @ingroup embedded
/// @brief Signature of the custom bare-metal panic handler callback.
using PanicHandlerFn = void (*)(const char* file, int line, const char* message) noexcept;

namespace internal {

inline PanicHandlerFn& getPanicHandler() noexcept {
    static PanicHandlerFn handler = nullptr;
    return handler;
}

inline void panic(const char* file, int line, const char* message) noexcept {
    if (auto fn = getPanicHandler()) {
        fn(file, line, message);
        return;
    }
#if defined(CORIUM_CUSTOM_PANIC)
    CORIUM_CUSTOM_PANIC(file, line, message);
#elif defined(__arm__) || defined(__thumb__)
    __asm volatile("bkpt #0");
    for (;;) {}
#else
    (void)std::fprintf(stderr, "Corium Panic: %s (%s:%d)\n", message, file, line);
    std::abort();
#endif
}

} // namespace internal

/// @ingroup embedded
/// @brief Configure the global bare-metal panic handler hook (e.g. for Hardware Fault logging).
inline void setPanicHandler(PanicHandlerFn fn) noexcept {
    internal::getPanicHandler() = fn;
}

} // namespace corium

#if defined(CORIUM_ENABLE_ASSERTIONS) || !defined(NDEBUG)
#define CORIUM_ASSERT(cond, msg) \
    do { \
        if (!(cond)) [[unlikely]] { \
            ::corium::internal::panic(__FILE__, __LINE__, msg); \
        } \
    } while (0)
#else
#define CORIUM_ASSERT(cond, msg) do { (void)sizeof(cond); } while (0)
#endif

#define CORIUM_PANIC(msg) ::corium::internal::panic(__FILE__, __LINE__, msg)

// <<< End: corium/internal/Panic.hpp

// >>> Begin: corium/policies/StoragePolicies.hpp
/**
 * @file StoragePolicies.hpp
 * @ingroup policies
 * @brief Static storage capacity policies for FastDelegate SBO inline buffers.
 */


#include <cstddef>

namespace corium {

/// @ingroup policies
/// @brief Policy configuring compile-time handler capacity and delegate inline storage size.
/// @tparam MaxHandlers Maximum handlers per event type stored statically.
/// @tparam InlineSize Maximum bytes for FastDelegate inline storage (zero heap allocation).
template <std::size_t MaxHandlers = 8, std::size_t InlineSize = 32>
struct FixedStoragePolicy {
    static constexpr std::size_t max_handlers_per_event = MaxHandlers;
    static constexpr std::size_t inline_storage_size = InlineSize;
};

/// @brief Default storage policy (8 handlers per event type, 32 bytes inline delegate storage).
using DefaultStoragePolicy = FixedStoragePolicy<8, 32>;

/// @brief Footprint-optimized storage policy (4 handlers per event type, 16 bytes inline storage).
using CompactStoragePolicy = FixedStoragePolicy<4, 16>;

/// @brief High-capacity storage policy (16 handlers per event type, 64 bytes inline storage).
using LargeStoragePolicy = FixedStoragePolicy<16, 64>;

/// @brief Zero-overhead storage policy for producer services or buses with zero event handlers.
using ZeroStoragePolicy = FixedStoragePolicy<0, 0>;

} // namespace corium

// <<< End: corium/policies/StoragePolicies.hpp

namespace corium {
namespace internal {

/// @brief Fixed-capacity stack-allocated list of event handlers for a single event type.
/// @tparam EventType Event type handled.
/// @tparam MaxHandlers Maximum number of handlers allowed for this event type.
/// @tparam InlineSize Max payload size in bytes for handler inline storage.
template <typename EventType, size_t MaxHandlers, size_t InlineSize>
class FixedHandlerList {
public:
    FixedHandlerList() = default;

    template <typename Handler>
    bool registerHandler(Handler&& handler) {
        if (_count >= MaxHandlers) {
            return false;
        }
        _handlers[_count] = EventHandlerDelegate<EventType, InlineSize>(std::forward<Handler>(handler));
        _count++;
        return true;
    }

    void dispatch(const EventType& event) const {
        for (size_t i = 0; i < _count; ++i) {
            _handlers[i].invoke(event);
        }
    }

    [[nodiscard]] size_t size() const noexcept {
        return _count;
    }

private:
    std::array<EventHandlerDelegate<EventType, InlineSize>, MaxHandlers> _handlers{};
    size_t _count = 0;
};

} // namespace internal

/// @brief Primary template declaration for BasicReactor.
template <typename EventVariant = DefaultEvents, typename StoragePolicy = DefaultStoragePolicy>
class BasicReactor;

/// @brief Static Event Reactor providing compile-time zero-heap dispatching.
/// @tparam Events Supported event types in std::variant.
/// @tparam StoragePolicy Configuration policy for handler capacity and inline storage size.
template <typename... Events, typename StoragePolicy>
class BasicReactor<std::variant<Events...>, StoragePolicy> {
    static constexpr size_t MaxHandlersPerEvent = StoragePolicy::max_handlers_per_event;
    static constexpr size_t InlineSize = StoragePolicy::inline_storage_size;

public:
    using EventVariant = std::variant<Events...>;
    using StoragePolicyType = StoragePolicy;

    BasicReactor() = default;
    ~BasicReactor() = default;

    BasicReactor(const BasicReactor&) = delete;
    BasicReactor& operator=(const BasicReactor&) = delete;

    BasicReactor(BasicReactor&&) noexcept = default;
    BasicReactor& operator=(BasicReactor&&) noexcept = default;

    /// @brief Register event handler for concrete event type with explicit type parameter.
    /// @tparam EventType Event type to handle.
    /// @tparam Handler Callable handler type.
    /// @param handler Callback to invoke when event is dispatched.
    /// @note Must be called before runtime.initialize() seals the reactor.
    template <typename EventType, typename Handler>
    bool registerHandler(Handler&& handler) {
        CORIUM_ASSERT(!_sealed, "registerHandler() called after reactor was sealed by runtime.initialize(). Move handler registration into onRegisterHandlers().");
        static_assert(has_variant_type_v<EventType, EventVariant>, "EventType is not part of EventVariant!");
        auto& list = std::get<internal::FixedHandlerList<EventType, MaxHandlersPerEvent, InlineSize>>(_handlers);
        return list.registerHandler(std::forward<Handler>(handler));
    }

    /// @brief Register event handler with automatic event type deduction from handler parameter signature.
    /// @tparam Handler Callable handler type (lambda, function pointer, or functor).
    /// @param handler Callback to invoke when event is dispatched.
    /// @note Must be called before runtime.initialize() seals the reactor.
    template <typename Handler>
    bool registerHandler(Handler&& handler) {
        CORIUM_ASSERT(!_sealed, "registerHandler() called after reactor was sealed by runtime.initialize(). Move handler registration into onRegisterHandlers().");
        using EventType = callable_event_type_t<Handler>;
        return registerHandler<EventType>(std::forward<Handler>(handler));
    }

    /// @brief Dispatch event via std::visit to concrete static handler array.
    /// @param event Event instance to dispatch.
    void dispatch(const EventVariant& event) const {
        std::visit([this](const auto& concreteEvent) {
            using EventType = std::decay_t<decltype(concreteEvent)>;
            const auto& list = std::get<internal::FixedHandlerList<EventType, MaxHandlersPerEvent, InlineSize>>(_handlers);
            list.dispatch(concreteEvent);
        }, event);
    }

    /// @brief Seal reactor handlers.
    void seal() noexcept {
        _sealed = true;
    }

    /// @brief Check if reactor has been sealed.
    [[nodiscard]] bool sealed() const noexcept {
        return _sealed;
    }

private:
    std::tuple<internal::FixedHandlerList<Events, MaxHandlersPerEvent, InlineSize>...> _handlers{};
    bool _sealed = false;
};

/// @brief Default Reactor alias using DefaultEvents and DefaultStoragePolicy.
using Reactor = BasicReactor<DefaultEvents>;

/// @brief Templated Reactor alias for custom event variant and storage policy.
template <typename EventVariant = DefaultEvents, typename StoragePolicy = DefaultStoragePolicy>
using ReactorT = BasicReactor<EventVariant, StoragePolicy>;

} // namespace corium

// <<< End: corium/internal/Reactor.hpp

// >>> Begin: corium/profiler/ProfilerPolicies.hpp
/**
 * @file ProfilerPolicies.hpp
 * @ingroup profiler
 * @brief Latency tracking and flight recording policies with runtime toggle.
 */


#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ostream>


// >>> Begin: corium/profiler/FlightRecorder.hpp
/**
 * @file FlightRecorder.hpp
 * @ingroup profiler
 * @brief Circular in-memory telemetry buffer with Chrome Tracing JSON export.
 */


#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ostream>

namespace corium::profiler {

/// @brief Timestamp clock used by the profiler (steady clock in nanoseconds).
using ProfilerClock = std::chrono::steady_clock;

/// @brief Represents a single traced event record in the circular flight recorder.
struct alignas(32) FlightRecord {
    std::size_t eventTypeId{0};
    const char* eventName{nullptr};
    uint64_t postTimestampNs{0};
    uint64_t dispatchTimestampNs{0};
    uint64_t finishTimestampNs{0};
    uint8_t priority{0};

    /// @brief Calculate latency spent waiting in queue before dispatch in microseconds.
    [[nodiscard]] double queueLatencyUs() const noexcept
    {
        if (dispatchTimestampNs < postTimestampNs) return 0.0;
        return static_cast<double>(dispatchTimestampNs - postTimestampNs) / 1000.0;
    }

    /// @brief Calculate duration of handler execution in microseconds.
    [[nodiscard]] double executionDurationUs() const noexcept
    {
        if (finishTimestampNs < dispatchTimestampNs) return 0.0;
        return static_cast<double>(finishTimestampNs - dispatchTimestampNs) / 1000.0;
    }

    /// @brief Calculate total turnaround time from post to finish in microseconds.
    [[nodiscard]] double totalDurationUs() const noexcept
    {
        if (finishTimestampNs < postTimestampNs) return 0.0;
        return static_cast<double>(finishTimestampNs - postTimestampNs) / 1000.0;
    }
};

/// @ingroup profiler
/// @brief Zero-heap circular flight recorder storing the last N event telemetry records.
/// Thread-safe for multiple producers and concurrent reader/dumping.
/// @tparam Capacity Number of flight records (power of 2).
template <std::size_t Capacity = 256>
class FlightRecorder {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2.");
    static constexpr std::size_t Mask = Capacity - 1;

public:
    FlightRecorder() = default;

    /// @brief Record a completed event execution into the circular buffer.
    void record(
        std::size_t eventTypeId,
        const char* eventName,
        uint64_t postTimeNs,
        uint64_t dispatchTimeNs,
        uint64_t finishTimeNs,
        uint8_t priority
    ) noexcept
    {
        const std::size_t idx = _writeIndex.fetch_add(1, std::memory_order_relaxed);
        FlightRecord& entry = _records[idx & Mask];
        entry.eventTypeId = eventTypeId;
        entry.eventName = eventName ? eventName : "UnknownEvent";
        entry.postTimestampNs = postTimeNs;
        entry.dispatchTimestampNs = dispatchTimeNs;
        entry.finishTimestampNs = finishTimeNs;
        entry.priority = priority;
    }

    /// @brief Get total number of recorded events since creation.
    [[nodiscard]] uint64_t totalRecorded() const noexcept
    {
        return _writeIndex.load(std::memory_order_relaxed);
    }

    /// @brief Get buffer capacity.
    [[nodiscard]] constexpr std::size_t capacity() const noexcept
    {
        return Capacity;
    }

    /// @brief Read a record by logical historical index (0 is oldest available in current window).
    [[nodiscard]] FlightRecord recordAt(std::size_t index) const noexcept
    {
        return _records[index & Mask];
    }

    /// @brief Iterate through available historical records in chronological order.
    template <typename Callback>
    void forEach(Callback&& cb) const
    {
        const uint64_t current = _writeIndex.load(std::memory_order_acquire);
        const std::size_t count = current < Capacity ? static_cast<std::size_t>(current) : Capacity;
        const uint64_t start = current < Capacity ? 0 : current - Capacity;

        for (std::size_t i = 0; i < count; ++i) {
            cb(_records[(start + i) & Mask]);
        }
    }

    /// @brief Export recorded flight logs in Chrome Tracing JSON format (supported by chrome://tracing and Perfetto UI).
    void exportChromeTracingJson(std::ostream& os) const
    {
        os << "[\n";
        bool first = true;

        forEach([&](const FlightRecord& rec) {
            if (!first) os << ",\n";
            first = false;

            // 1. Queue wait event (phase "X" complete event)
            const double postUs = static_cast<double>(rec.postTimestampNs) / 1000.0;
            const double queueDurationUs = rec.queueLatencyUs();
            const double dispatchUs = static_cast<double>(rec.dispatchTimestampNs) / 1000.0;
            const double execDurationUs = rec.executionDurationUs();

            os << "  {\"name\": \"QueueWait[" << (rec.eventName ? rec.eventName : "Event")
               << "]\", \"cat\": \"corium\", \"ph\": \"X\", \"ts\": " << postUs
               << ", \"dur\": " << queueDurationUs << ", \"pid\": 1, \"tid\": 1, \"args\": {\"prio\": "
               << static_cast<int>(rec.priority) << "}},\n";

            // 2. Dispatch / Handler execution event
            os << "  {\"name\": \"Dispatch[" << (rec.eventName ? rec.eventName : "Event")
               << "]\", \"cat\": \"corium\", \"ph\": \"X\", \"ts\": " << dispatchUs
               << ", \"dur\": " << execDurationUs << ", \"pid\": 1, \"tid\": 1, \"args\": {\"type_id\": "
               << rec.eventTypeId << "}}";
        });

        os << "\n]\n";
    }

private:
    std::atomic<uint64_t> _writeIndex{0};
    std::array<FlightRecord, Capacity> _records{};
};

} // namespace corium::profiler

// <<< End: corium/profiler/FlightRecorder.hpp

namespace corium::profiler {

/// @brief Default Profiler Policy: Zero-overhead, completely compiled out by inline empty functions.
struct NullProfiler {
    constexpr NullProfiler() noexcept = default;

    template <typename EventVariant>
    void onEventPosted(const EventVariant&, uint8_t) noexcept {}

    template <typename EventVariant>
    void onEventDispatched(
        const EventVariant&,
        uint8_t,
        uint64_t /*postTimeNs*/,
        uint64_t /*dispatchTimeNs*/,
        uint64_t /*finishTimeNs*/
    ) noexcept {}

    [[nodiscard]] static constexpr uint64_t nowNs() noexcept { return 0; }

    /// @brief No-op: NullProfiler does not track post timestamps.
    void recordPostTime(uint64_t) noexcept {}

    /// @brief No-op: always returns 0 (NullProfiler has no timestamps).
    [[nodiscard]] uint64_t takePostTime() noexcept { return 0; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Internal: parallel timestamp ring buffer for tracking per-event post times.
//
// Uses a separate MpscRingBuffer<uint64_t, Capacity> that is pushed in lockstep
// with the main event queue. Because the event bus is MPSC and dispatch is
// single-consumer (same thread that calls pump()), timestamps arrive and are
// consumed in FIFO order, so the i-th timestamp always belongs to the i-th event.
//
// Zero-overhead when used with NullProfiler (the entire type is not instantiated).
// ─────────────────────────────────────────────────────────────────────────────
template <std::size_t Capacity = 1024>
class PostTimestampQueue {
public:
    /// @brief Record the post timestamp for an event being pushed into the event queue.
    void recordPostTime(uint64_t postNs) noexcept
    {
        // Best-effort: if the timestamp queue is full (e.g. profiler not being read),
        // drop the timestamp rather than blocking or corrupting the event queue.
        _timestamps.tryPush(postNs);
    }

    /// @brief Pop and return the oldest recorded post timestamp (called at dispatch time).
    /// @return The post timestamp, or 0 if the queue is empty (should not happen in steady state).
    [[nodiscard]] uint64_t takePostTime() noexcept
    {
        uint64_t ts = 0;
        _timestamps.tryPop(ts);
        return ts;
    }

private:
    MpscRingBuffer<uint64_t, Capacity> _timestamps;
};

// ─────────────────────────────────────────────────────────────────────────────

/// @ingroup profiler
/// @brief Real-time event latency and performance statistics tracker.
/// Zero dynamic memory allocation. Tracks min, max, total queue latency and handler duration.
/// @tparam QueueCapacity Capacity of the parallel post-timestamp ring buffer (must match
///         the event queue capacity for accurate per-event latency tracking).
template <std::size_t QueueCapacity = 1024>
class LatencyTracker {
public:
    LatencyTracker() noexcept = default;

    [[nodiscard]] static uint64_t nowNs() noexcept
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                ProfilerClock::now().time_since_epoch()
            ).count()
        );
    }

    /// @brief Record the wall-clock post timestamp for the event being pushed now.
    /// Called by EventBus::post() immediately after the event enters the queue.
    void recordPostTime(uint64_t postNs) noexcept
    {
        if (!_enabled.load(std::memory_order_relaxed)) return;
        _postTimestamps.recordPostTime(postNs);
    }

    /// @brief Pop and return the oldest post timestamp (called at dispatch time by EventBus).
    [[nodiscard]] uint64_t takePostTime() noexcept
    {
        return _postTimestamps.takePostTime();
    }

    template <typename EventVariant>
    void onEventPosted(const EventVariant&, uint8_t) noexcept
    {
        if (!_enabled.load(std::memory_order_relaxed)) return;
        _totalPosted.fetch_add(1, std::memory_order_relaxed);
    }

    template <typename EventVariant>
    void onEventDispatched(
        const EventVariant&,
        uint8_t,
        uint64_t postTimeNs,
        uint64_t dispatchTimeNs,
        uint64_t finishTimeNs
    ) noexcept
    {
        if (!_enabled.load(std::memory_order_relaxed)) return;

        const uint64_t queueLatencyNs = (dispatchTimeNs > postTimeNs) ? (dispatchTimeNs - postTimeNs) : 0;
        const uint64_t execDurationNs = (finishTimeNs > dispatchTimeNs) ? (finishTimeNs - dispatchTimeNs) : 0;

        _totalDispatched.fetch_add(1, std::memory_order_relaxed);
        _totalQueueLatencyNs.fetch_add(queueLatencyNs, std::memory_order_relaxed);
        _totalExecDurationNs.fetch_add(execDurationNs, std::memory_order_relaxed);

        // Update Max Latency
        uint64_t currentMaxLat = _maxQueueLatencyNs.load(std::memory_order_relaxed);
        while (queueLatencyNs > currentMaxLat &&
               !_maxQueueLatencyNs.compare_exchange_weak(currentMaxLat, queueLatencyNs, std::memory_order_relaxed)) {}

        // Update Min Latency
        uint64_t currentMinLat = _minQueueLatencyNs.load(std::memory_order_relaxed);
        while (queueLatencyNs < currentMinLat &&
               !_minQueueLatencyNs.compare_exchange_weak(currentMinLat, queueLatencyNs, std::memory_order_relaxed)) {}

        // Update Max Execution Duration
        uint64_t currentMaxExec = _maxExecDurationNs.load(std::memory_order_relaxed);
        while (execDurationNs > currentMaxExec &&
               !_maxExecDurationNs.compare_exchange_weak(currentMaxExec, execDurationNs, std::memory_order_relaxed)) {}
    }

    /// @brief Enable or disable runtime latency profiling.
    void setEnabled(bool enabled) noexcept
    {
        _enabled.store(enabled, std::memory_order_release);
    }

    /// @brief Enable runtime profiling.
    void enable() noexcept { setEnabled(true); }

    /// @brief Disable runtime profiling.
    void disable() noexcept { setEnabled(false); }

    /// @brief Check if runtime profiling is enabled.
    [[nodiscard]] bool isEnabled() const noexcept
    {
        return _enabled.load(std::memory_order_acquire);
    }

    /// @brief Total count of posted events.
    [[nodiscard]] uint64_t totalPosted() const noexcept
    {
        return _totalPosted.load(std::memory_order_relaxed);
    }

    /// @brief Total count of dispatched events.
    [[nodiscard]] uint64_t totalDispatched() const noexcept
    {
        return _totalDispatched.load(std::memory_order_relaxed);
    }

    /// @brief Minimum queue latency in microseconds.
    [[nodiscard]] double minQueueLatencyUs() const noexcept
    {
        const uint64_t val = _minQueueLatencyNs.load(std::memory_order_relaxed);
        return val == UINT64_MAX ? 0.0 : static_cast<double>(val) / 1000.0;
    }

    /// @brief Maximum queue latency in microseconds.
    [[nodiscard]] double maxQueueLatencyUs() const noexcept
    {
        return static_cast<double>(_maxQueueLatencyNs.load(std::memory_order_relaxed)) / 1000.0;
    }

    /// @brief Average queue latency in microseconds.
    [[nodiscard]] double averageQueueLatencyUs() const noexcept
    {
        const uint64_t count = _totalDispatched.load(std::memory_order_relaxed);
        if (count == 0) return 0.0;
        return static_cast<double>(_totalQueueLatencyNs.load(std::memory_order_relaxed)) / (static_cast<double>(count) * 1000.0);
    }

    /// @brief Maximum handler execution duration in microseconds.
    [[nodiscard]] double maxExecutionDurationUs() const noexcept
    {
        return static_cast<double>(_maxExecDurationNs.load(std::memory_order_relaxed)) / 1000.0;
    }

    /// @brief Average handler execution duration in microseconds.
    [[nodiscard]] double averageExecutionDurationUs() const noexcept
    {
        const uint64_t count = _totalDispatched.load(std::memory_order_relaxed);
        if (count == 0) return 0.0;
        return static_cast<double>(_totalExecDurationNs.load(std::memory_order_relaxed)) / (static_cast<double>(count) * 1000.0);
    }

    /// @brief Reset all accumulated statistics.
    void resetStats() noexcept
    {
        _totalPosted.store(0, std::memory_order_relaxed);
        _totalDispatched.store(0, std::memory_order_relaxed);
        _totalQueueLatencyNs.store(0, std::memory_order_relaxed);
        _totalExecDurationNs.store(0, std::memory_order_relaxed);
        _maxQueueLatencyNs.store(0, std::memory_order_relaxed);
        _minQueueLatencyNs.store(UINT64_MAX, std::memory_order_relaxed);
        _maxExecDurationNs.store(0, std::memory_order_relaxed);
    }

private:
    PostTimestampQueue<QueueCapacity> _postTimestamps;

    std::atomic<bool> _enabled{true};
    std::atomic<uint64_t> _totalPosted{0};
    std::atomic<uint64_t> _totalDispatched{0};
    std::atomic<uint64_t> _totalQueueLatencyNs{0};
    std::atomic<uint64_t> _totalExecDurationNs{0};
    std::atomic<uint64_t> _maxQueueLatencyNs{0};
    std::atomic<uint64_t> _minQueueLatencyNs{UINT64_MAX};
    std::atomic<uint64_t> _maxExecDurationNs{0};
};

/// @brief Combined Flight Recorder and Latency Tracker Profiler.
/// Records historical event traces into a circular in-memory buffer with zero heap allocations.
/// @tparam BufferCapacity Capacity of circular flight recorder (power of 2, e.g. 128, 256, 1024).
/// @tparam QueueCapacity  Capacity of the parallel post-timestamp ring buffer (should match the
///         event queue capacity for accurate latency measurement; default 1024).
template <std::size_t BufferCapacity = 256, std::size_t QueueCapacity = 1024>
class FlightRecorderProfiler : public LatencyTracker<QueueCapacity> {
public:
    FlightRecorderProfiler() = default;

    template <typename EventVariant>
    void onEventDispatched(
        const EventVariant& event,
        uint8_t priority,
        uint64_t postTimeNs,
        uint64_t dispatchTimeNs,
        uint64_t finishTimeNs
    ) noexcept
    {
        if (!this->isEnabled()) return;

        LatencyTracker<QueueCapacity>::onEventDispatched(event, priority, postTimeNs, dispatchTimeNs, finishTimeNs);

        const std::size_t typeIndex = event.index();
        const char* name = "Event";

        _flightRecorder.record(typeIndex, name, postTimeNs, dispatchTimeNs, finishTimeNs, priority);
    }

    /// @brief Access reference to underlying circular flight recorder.
    [[nodiscard]] const FlightRecorder<BufferCapacity>& flightRecorder() const noexcept
    {
        return _flightRecorder;
    }

    /// @brief Export flight recorder traces to Chrome Tracing JSON.
    void exportChromeTracingJson(std::ostream& os) const
    {
        _flightRecorder.exportChromeTracingJson(os);
    }

private:
    FlightRecorder<BufferCapacity> _flightRecorder;
};

} // namespace corium::profiler

// <<< End: corium/profiler/ProfilerPolicies.hpp

#include <utility>

namespace corium {

/// @ingroup core
/// @brief Policy-configurable non-virtual event bus implementation.
/// @tparam EventVariantType The variant type list of supported events.
/// @tparam QueuePolicy Strategy for queueing events (bounded lock-free MPSC).
/// @tparam SignalPolicy Strategy for signaling (NoSignalPolicy by default).
/// @tparam StoragePolicy Strategy for compile-time handler capacity and delegate storage.
/// @tparam OverflowPolicy Strategy for queue overflow handling.
/// @tparam ProfilerPolicy Strategy for latency telemetry and trace recording (NullProfiler by default).
template <
    typename EventVariantType = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariantType, 1024>,
    typename SignalPolicy = NoSignalPolicy,
    typename StoragePolicy = DefaultStoragePolicy,
    typename OverflowPolicy = DropNewestOverflowPolicy,
    typename ProfilerPolicy = profiler::NullProfiler
>
class BasicEventBus {
public:
    using EventVariant = EventVariantType;
    using ReactorType = BasicReactor<EventVariant, StoragePolicy>;
    using ProfilerPolicyType = ProfilerPolicy;

    BasicEventBus() = default;

    /// @brief Post an event into the queue with optional priority (rvalue overload).
    /// @param event Movable event variant instance.
    /// @param priority Priority tier (EventPriority::High, Normal, Low).
    /// @note Thread-safe, lock-free, zero heap allocation.
    /// @see EventSinkT for non-owning posting handle.
    void post(EventVariant&& event, EventPriority priority = EventPriority::Normal)
    {
        _profilerPolicy.onEventPosted(event, static_cast<uint8_t>(priority));
        _profilerPolicy.recordPostTime(_profilerPolicy.nowNs());
        _eventQueue.pushEvent(std::move(event), priority);
    }

    /// @brief Post an event into the queue with optional priority (const lvalue overload).
    /// @param event Const reference to event variant instance.
    /// @param priority Priority tier (EventPriority::High, Normal, Low).
    /// @note Thread-safe, lock-free, zero heap allocation.
    void post(const EventVariant& event, EventPriority priority = EventPriority::Normal)
    {
        _profilerPolicy.onEventPosted(event, static_cast<uint8_t>(priority));
        _profilerPolicy.recordPostTime(_profilerPolicy.nowNs());
        _eventQueue.pushEvent(event, priority);
    }

    /// @brief Convenience helper for posting high-priority events.
    /// @tparam Event Concrete event type convertible to EventVariant.
    /// @param event Event payload to forward at EventPriority::High.
    /// @note Guaranteed to be dispatched ahead of standard Normal/Low priority events.
    template <typename Event>
    void postHighPriority(Event&& event)
    {
        post(EventVariant(std::forward<Event>(event)), EventPriority::High);
    }

    /// @brief Process a single event from the queue.
    /// @return true if an event was popped and dispatched; false if queue was empty.
    bool processOne()
    {
        EventVariant event;
        if (!_eventQueue.tryPopEvent(event)) {
            return false;
        }
        const uint64_t postTime     = _profilerPolicy.takePostTime();
        const uint64_t dispatchTime = _profilerPolicy.nowNs();
        _reactor.dispatch(event);
        const uint64_t finishTime = _profilerPolicy.nowNs();
        _profilerPolicy.onEventDispatched(event, 0, postTime, dispatchTime, finishTime);
        return true;
    }

    /// @brief Process up to maxBatch events consecutively from the queue.
    /// @param maxBatch Maximum number of events to process in this batch.
    /// @return Number of events successfully popped and dispatched.
    std::size_t processBatch(std::size_t maxBatch)
    {
        std::size_t count = 0;
        EventVariant event;
        while (count < maxBatch) {
            if (!_eventQueue.tryPopEvent(event)) {
                break;
            }
            const uint64_t postTime     = _profilerPolicy.takePostTime();
            const uint64_t dispatchTime = _profilerPolicy.nowNs();
            _reactor.dispatch(event);
            const uint64_t finishTime = _profilerPolicy.nowNs();
            _profilerPolicy.onEventDispatched(event, 0, postTime, dispatchTime, finishTime);
            count++;
        }
        return count;
    }

    /// @brief Drain and dispatch all currently enqueued events.
    /// @return Total number of events processed.
    std::size_t drain()
    {
        std::size_t total = 0;
        while (processOne()) {
            total++;
        }
        return total;
    }

    /// @brief Access reference to profiler policy.
    [[nodiscard]] ProfilerPolicy& profiler() noexcept
    {
        return _profilerPolicy;
    }

    /// @brief Access const reference to profiler policy.
    [[nodiscard]] const ProfilerPolicy& profiler() const noexcept
    {
        return _profilerPolicy;
    }

    /// @brief Check if event queue is empty.
    [[nodiscard]] bool empty() const
    {
        return _eventQueue.empty();
    }

    /// @brief Seal reactor handlers.
    void seal()
    {
        _reactor.seal();
    }

    /// @brief Set static callback for event availability when queue transitions to non-empty.
    void setOnQueueNonEmpty(StaticCallback callback)
    {
        _eventQueue.setOnQueueNonEmpty(callback);
    }

    /// @brief Register an event handler with explicit event type parameter.
    /// @tparam EventType Event type to handle.
    /// @tparam Handler Callable handler type.
    /// @param handler Callback to invoke when event occurs.
    template <typename EventType, typename Handler>
    bool registerHandler(Handler&& handler)
    {
        return _reactor.template registerHandler<EventType>(std::forward<Handler>(handler));
    }

    /// @brief Register an event handler with automatic event type deduction.
    /// @tparam Handler Callable handler type (lambda, function pointer, or functor).
    /// @param handler Callback to invoke when event occurs.
    template <typename Handler>
    bool registerHandler(Handler&& handler)
    {
        using EventType = callable_event_type_t<Handler>;
        return _reactor.template registerHandler<EventType>(std::forward<Handler>(handler));
    }

    /// @brief Access reference to signal policy.
    [[nodiscard]] SignalPolicy& signalPolicy() noexcept
    {
        return _eventQueue.signalPolicy();
    }

    /// @brief Access const reference to signal policy.
    [[nodiscard]] const SignalPolicy& signalPolicy() const noexcept
    {
        return _eventQueue.signalPolicy();
    }

    /// @brief Access reference to overflow policy.
    [[nodiscard]] OverflowPolicy& overflowPolicy() noexcept
    {
        return _eventQueue.overflowPolicy();
    }

    /// @brief Access const reference to overflow policy.
    [[nodiscard]] const OverflowPolicy& overflowPolicy() const noexcept
    {
        return _eventQueue.overflowPolicy();
    }

    /// @brief Access reference to reactor.
    ReactorType& reactor() noexcept
    {
        return _reactor;
    }

    /// @brief Get an EventSink handle pointing to this event bus.
    EventSinkT<EventVariant> sink() noexcept
    {
        return EventSinkT<EventVariant>(*this);
    }

private:
    EventQueue<QueuePolicy, SignalPolicy, OverflowPolicy> _eventQueue;
    ReactorType _reactor;
    ProfilerPolicy _profilerPolicy{};
};

/// @brief Default EventBus alias using DefaultEvents and NoSignalPolicy.
using EventBus = BasicEventBus<DefaultEvents>;

/// @brief Templated EventBus alias for custom event variant and policies.
template <
    typename EventVariantType = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariantType, 1024>,
    typename SignalPolicy = NoSignalPolicy,
    typename StoragePolicy = DefaultStoragePolicy,
    typename OverflowPolicy = DropNewestOverflowPolicy,
    typename ProfilerPolicy = profiler::NullProfiler
>
using EventBusT = BasicEventBus<EventVariantType, QueuePolicy, SignalPolicy, StoragePolicy, OverflowPolicy, ProfilerPolicy>;

} // namespace corium

// <<< End: corium/EventBus.hpp

// >>> Begin: corium/policies/TimerPolicies.hpp
/**
 * @file TimerPolicies.hpp
 * @ingroup policies
 * @brief Timer scheduler static capacity and storage policies.
 */


#include <cstddef>

namespace corium {

/// @brief Compile-time policy configuring TimerScheduler capacity and clock source.
/// @tparam MaxTimers Maximum number of concurrent timers allowed (default: 64).
/// @tparam ClockPolicy Time source and arithmetic policy (default: ChronoClockPolicy).
template <size_t MaxTimers = 64, typename ClockPolicy = ChronoClockPolicy>
struct FixedTimerStoragePolicy {
    static constexpr size_t max_timers = MaxTimers;
    using clock_policy = ClockPolicy;
};

using DefaultTimerStoragePolicy = FixedTimerStoragePolicy<64, ChronoClockPolicy>;
using CompactTimerStoragePolicy = FixedTimerStoragePolicy<16, ChronoClockPolicy>;
using LargeTimerStoragePolicy = FixedTimerStoragePolicy<256, ChronoClockPolicy>;

} // namespace corium

// <<< End: corium/policies/TimerPolicies.hpp

namespace corium {

template <typename Derived, typename EventVariant, std::size_t MaxServices>
class Application;

/// @ingroup core
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
    using EventVariantType = EventVariant;
    using EventType = EventVariant;
    using EventBusType = BasicEventBus<EventVariant, QueuePolicy, SignalPolicy, StoragePolicy, OverflowPolicy, ProfilerPolicy>;
    using ClockPolicyType = typename internal::get_timer_clock_policy<TimerStoragePolicy>::type;
    using TimerSchedulerType = TimerScheduler<EventVariant, TimerStoragePolicy::max_timers, ClockPolicyType>;
    using ProfilerPolicyType = ProfilerPolicy;

    enum class State : uint8_t {
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

    /// @brief Detach application to prevent dangling callbacks on shutdown.
    void detachApplication() noexcept
    {
        _appShutdownCb = StaticCallback{};
    }

    /// @brief Initialize runtime with target application using static CRTP dispatch.
    /// @tparam Derived Application core type deriving from Application<Derived, AppEvents, MaxServices>.
    /// @tparam AppEvents Event variant or event bus type defined on the application.
    /// @tparam MaxServices Number of services the application can register (deduced automatically).
    /// @param application Application instance to initialize.
    template <typename Derived, typename AppEvents = EventVariant, std::size_t MaxServices = 8>
    void initialize(corium::Application<Derived, AppEvents, MaxServices>& application)
    {
        using AppEventVariant = typename corium::Application<Derived, AppEvents, MaxServices>::EventVariant;
        static_assert(std::is_same_v<AppEventVariant, EventVariant>,
            "Application EventVariant list must match Runtime EventVariant list!");

        _state.store(State::Initializing, std::memory_order_release);
        _appShutdownCb = StaticCallback{
            [](void* appPtr) {
                auto* app = static_cast<Derived*>(static_cast<corium::Application<Derived, AppEvents, MaxServices>*>(appPtr));
                app->shutdownServices();
                app->shutdown();
                app->resetContext();
            },
            &application
        };

        auto ctx = applicationContext();
        ctx.setTimerScheduler(_timerScheduler);
        ctx.setRuntimeDetach(this, [](void* rt) noexcept {
            static_cast<BasicRuntime*>(rt)->detachApplication();
        });
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
            auto cb = _appShutdownCb;
            _appShutdownCb = StaticCallback{};
            cb();
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
    [[nodiscard]] SignalPolicy& signalPolicy() noexcept
    {
        return _eventBus.signalPolicy();
    }

    /// @brief Access const reference to signal policy.
    [[nodiscard]] const SignalPolicy& signalPolicy() const noexcept
    {
        return _eventBus.signalPolicy();
    }

    /// @brief Access reference to overflow policy.
    [[nodiscard]] OverflowPolicy& overflowPolicy() noexcept
    {
        return _eventBus.overflowPolicy();
    }

    /// @brief Access const reference to overflow policy.
    [[nodiscard]] const OverflowPolicy& overflowPolicy() const noexcept
    {
        return _eventBus.overflowPolicy();
    }

    /// @brief Access reference to timer scheduler.
    [[nodiscard]] TimerSchedulerType& timerScheduler() noexcept
    {
        return _timerScheduler;
    }

    /// @brief Access const reference to timer scheduler.
    [[nodiscard]] const TimerSchedulerType& timerScheduler() const noexcept
    {
        return _timerScheduler;
    }

    /// @brief Access event sink handle.
    [[nodiscard]] EventSinkT<EventVariant> eventSink() noexcept
    {
        return _eventBus.sink();
    }

    /// @brief Access reference to profiler policy.
    [[nodiscard]] ProfilerPolicyType& profiler() noexcept
    {
        return _eventBus.profiler();
    }

    /// @brief Access const reference to profiler policy.
    [[nodiscard]] const ProfilerPolicyType& profiler() const noexcept
    {
        return _eventBus.profiler();
    }

private:
    /// @brief Create ApplicationContext for application wiring.
    ApplicationContext<EventVariant> applicationContext()
    {
        auto ctx = ApplicationContext<EventVariant>{
            _eventBus,
            StaticCallback{
                [](void* c) { static_cast<BasicRuntime*>(c)->requestQuit(); },
                this
            }
        };
        ctx.setTimerScheduler(_timerScheduler);
        return ctx;
    }

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


// >>> Begin: corium/RuntimeBuilder.hpp
/**
 * @file RuntimeBuilder.hpp
 * @ingroup core
 * @brief Fluent compile-time builder for custom policy-configured runtimes.
 */


#include <cstddef>

namespace corium {

template <
    typename EventVariant,
    typename QueuePolicy,
    typename SignalPolicy,
    typename StoragePolicy,
    typename OverflowPolicy,
    typename TimerStoragePolicy,
    typename ProfilerPolicy
>
class BasicRuntime;

namespace internal {

/// @brief Compile-time helper to round up an integer to the next power of 2.
template <std::size_t N>
constexpr std::size_t next_power_of_two() {
    if (N <= 1) return 1;
    uint64_t val = N - 1;
    val |= val >> 1;
    val |= val >> 2;
    val |= val >> 4;
    val |= val >> 8;
    val |= val >> 16;
    val |= val >> 32;
    return static_cast<std::size_t>(val + 1);
}

/// @brief Extract queue capacity from a QueuePolicy if it exposes a static ::capacity member,
/// rounded up to the nearest power of 2 for power-of-two ring buffer constraints.
/// Falls back to 1024 for policies without a capacity (e.g. NoQueuePolicy).
template <typename QueuePolicy, typename = void>
struct queue_capacity_of : std::integral_constant<std::size_t, 1024> {};

template <typename QueuePolicy>
struct queue_capacity_of<QueuePolicy, std::void_t<decltype(QueuePolicy::capacity)>>
    : std::integral_constant<std::size_t, next_power_of_two<QueuePolicy::capacity>()> {};

template <typename QueuePolicy>
static constexpr std::size_t queue_capacity_of_v = queue_capacity_of<QueuePolicy>::value;

/// @brief Rebind QueuePolicy EventVariant type parameter while preserving Capacity or Policy structure.
template <typename QueuePolicy, typename NewEventVariant>
struct rebind_queue_policy;

template <template <typename, size_t> class QueuePolicy, typename OldEventVariant, size_t Capacity, typename NewEventVariant>
struct rebind_queue_policy<QueuePolicy<OldEventVariant, Capacity>, NewEventVariant> {
    using type = QueuePolicy<NewEventVariant, Capacity>;
};

template <template <typename, size_t, size_t, size_t> class QueuePolicy, typename OldEventVariant, size_t HighCap, size_t NormalCap, size_t LowCap, typename NewEventVariant>
struct rebind_queue_policy<QueuePolicy<OldEventVariant, HighCap, NormalCap, LowCap>, NewEventVariant> {
    using type = QueuePolicy<NewEventVariant, HighCap, NormalCap, LowCap>;
};

template <template <typename> class QueuePolicy, typename OldEventVariant, typename NewEventVariant>
struct rebind_queue_policy<QueuePolicy<OldEventVariant>, NewEventVariant> {
    using type = QueuePolicy<NewEventVariant>;
};

template <typename QueuePolicy, typename NewEventVariant>
using rebind_queue_policy_t = typename rebind_queue_policy<QueuePolicy, NewEventVariant>::type;

/// @brief Rebind QueuePolicy Capacity while preserving EventVariant.
template <typename QueuePolicy, size_t NewCapacity>
struct rebind_queue_capacity;

template <template <typename, size_t> class QueuePolicy, typename EventVariant, size_t OldCapacity, size_t NewCapacity>
struct rebind_queue_capacity<QueuePolicy<EventVariant, OldCapacity>, NewCapacity> {
    using type = QueuePolicy<EventVariant, NewCapacity>;
};

template <template <typename, size_t, size_t, size_t> class QueuePolicy, typename EventVariant, size_t HighCap, size_t OldNormalCap, size_t LowCap, size_t NewCapacity>
struct rebind_queue_capacity<QueuePolicy<EventVariant, HighCap, OldNormalCap, LowCap>, NewCapacity> {
    using type = QueuePolicy<EventVariant, HighCap, NewCapacity, LowCap>;
};

template <typename QueuePolicy, size_t NewCapacity>
using rebind_queue_capacity_t = typename rebind_queue_capacity<QueuePolicy, NewCapacity>::type;

} // namespace internal

/// @ingroup core
/// @brief Primary template implementation for fluent compile-time Runtime construction.
template <
    typename EventVariant = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariant, 1024>,
    typename SignalPolicy = NoSignalPolicy,
    typename StoragePolicy = DefaultStoragePolicy,
    typename OverflowPolicy = DropNewestOverflowPolicy,
    typename TimerStoragePolicy = DefaultTimerStoragePolicy,
    typename ProfilerPolicy = profiler::NullProfiler
>
struct BasicRuntimeBuilder {
    /// @brief Specify custom event variant list type (preserves existing queue capacity/policy).
    template <typename NewEventVariant>
    using WithEvents = BasicRuntimeBuilder<
        NewEventVariant,
        internal::rebind_queue_policy_t<QueuePolicy, NewEventVariant>,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify custom queue capacity for bounded MPSC queue (preserves existing event variant).
    template <size_t Capacity>
    using WithCapacity = BasicRuntimeBuilder<
        EventVariant,
        internal::rebind_queue_capacity_t<QueuePolicy, Capacity>,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Switch queue policy to PriorityMpscQueuePolicy with specified high and normal capacities.
    template <size_t HighCapacity = 256, size_t NormalCapacity = 1024>
    using WithPriorityQueue = BasicRuntimeBuilder<
        EventVariant,
        PriorityMpscQueuePolicy<EventVariant, HighCapacity, NormalCapacity>,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify custom QueuePolicy.
    template <typename NewQueuePolicy>
    using WithQueuePolicy = BasicRuntimeBuilder<
        EventVariant,
        NewQueuePolicy,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify custom SignalPolicy.
    template <typename NewSignalPolicy>
    using WithSignalPolicy = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        NewSignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify custom StoragePolicy.
    template <typename NewStoragePolicy>
    using WithStoragePolicy = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        NewStoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify custom OverflowPolicy.
    template <typename NewOverflowPolicy>
    using WithOverflowPolicy = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        StoragePolicy,
        NewOverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify maximum number of concurrent active timers.
    template <size_t MaxTimers>
    using WithMaxTimers = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        FixedTimerStoragePolicy<MaxTimers>,
        ProfilerPolicy
    >;

    /// @brief Specify custom ClockPolicy (time source strategy).
    template <typename NewClockPolicy>
    using WithClockPolicy = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        FixedTimerStoragePolicy<TimerStoragePolicy::max_timers, NewClockPolicy>,
        ProfilerPolicy
    >;

    /// @brief Specify custom TimerStoragePolicy.
    template <typename NewTimerStoragePolicy>
    using WithTimerStoragePolicy = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        NewTimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify maximum handlers per event type (rebinds StoragePolicy).
    template <size_t NewMaxHandlers>
    using WithMaxHandlersPerEvent = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        FixedStoragePolicy<NewMaxHandlers, StoragePolicy::inline_storage_size>,
        OverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify inline storage size for FastDelegate (rebinds StoragePolicy).
    template <size_t NewInlineSize>
    using WithInlineSize = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        FixedStoragePolicy<StoragePolicy::max_handlers_per_event, NewInlineSize>,
        OverflowPolicy,
        TimerStoragePolicy,
        ProfilerPolicy
    >;

    /// @brief Specify custom ProfilerPolicy strategy.
    template <typename NewProfilerPolicy>
    using WithProfiler = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        NewProfilerPolicy
    >;

    /// @brief Convenience helper: Configure FlightRecorder circular trace logger.
    /// The QueueCapacity of the internal timestamp ring buffer is automatically derived from
    /// the current QueuePolicy capacity, ensuring accurate per-event latency measurement.
    template <std::size_t BufferCapacity = 256>
    using WithFlightRecorder = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        profiler::FlightRecorderProfiler<BufferCapacity, internal::queue_capacity_of_v<QueuePolicy>>
    >;

    /// @brief Convenience helper: Configure real-time event latency statistics tracker.
    /// The QueueCapacity of the internal timestamp ring buffer is automatically derived from
    /// the current QueuePolicy capacity, ensuring accurate per-event latency measurement.
    template <std::size_t BufferCapacity = 256>
    using WithLatencyTracker = BasicRuntimeBuilder<
        EventVariant,
        QueuePolicy,
        SignalPolicy,
        StoragePolicy,
        OverflowPolicy,
        TimerStoragePolicy,
        profiler::LatencyTracker<internal::queue_capacity_of_v<QueuePolicy>>
    >;

    /// @brief Complete builder configuration and return BasicRuntime type.
    using Build = BasicRuntime<EventVariant, QueuePolicy, SignalPolicy, StoragePolicy, OverflowPolicy, TimerStoragePolicy, ProfilerPolicy>;
};

/// @ingroup core
/// @brief Fluent compile-time builder for configuring BasicRuntime.
/// Usage: `using MyRuntime = corium::RuntimeBuilder::WithEvents<MyEvents>::Build;`
struct RuntimeBuilder : BasicRuntimeBuilder<> {};

} // namespace corium

// <<< End: corium/RuntimeBuilder.hpp

// <<< End: corium/Runtime.hpp

// >>> Begin: corium/Application.hpp
/**
 * @file Application.hpp
 * @ingroup core
 * @brief CRTP static polymorphism application base class with auto-deduced event handlers.
 */



// >>> Begin: corium/ServiceRegistry.hpp
/**
 * @file ServiceRegistry.hpp
 * @ingroup core
 * @brief Fixed-capacity static container for background worker services.
 */


#include <array>
#include <cstddef>
#include <stop_token>


// >>> Begin: corium/ServiceContext.hpp
/**
 * @file ServiceContext.hpp
 * @ingroup core
 * @brief Dependency injection context for background services.
 */



namespace corium {

/// @brief Execution context provided to background services.
/// Allows services to post events back to the main event queue thread-safely
/// and communicate with other registered background services.
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
class BasicServiceContext {
public:
    using GetServiceFn = void* (*)(void* registryPtr, internal::TypeIdPtr typeId);

    BasicServiceContext() = default;

    explicit BasicServiceContext(
        EventSinkT<EventVariant> sink,
        void* regPtr = nullptr,
        GetServiceFn fn = nullptr
    ) noexcept
        : _eventSink(sink), _registryPtr(regPtr), _getServiceFn(fn)
    {}

    /// @brief Access main application EventSink handle.
    [[nodiscard]] EventSinkT<EventVariant> mainSink() const noexcept
    {
        return _eventSink;
    }

    /// @brief Access main application EventSink handle.
    [[nodiscard]] EventSinkT<EventVariant> eventSink() const noexcept
    {
        return _eventSink;
    }

    /// @brief Configure registry resolution bridge (internal framework usage).
    void setRegistry(void* registryPtr, GetServiceFn fn) noexcept
    {
        _registryPtr = registryPtr;
        _getServiceFn = fn;
    }

    /// @brief Retrieve another registered service instance by type.
    /// @tparam ServiceType Type of the target background service.
    /// @return Pointer to ServiceType instance or nullptr if not registered.
    template <typename ServiceType>
    [[nodiscard]] ServiceType* getService() const noexcept
    {
        if (_registryPtr && _getServiceFn) {
            return static_cast<ServiceType*>(_getServiceFn(_registryPtr, internal::getTypeId<ServiceType>()));
        }
        return nullptr;
    }

    /// @brief Retrieve event sink handle of a target registered service if available.
    /// @tparam ServiceType Type of the target background service.
    /// @return EventSinkT handle targeting the service's incoming queue, or empty handle if unavailable.
    template <typename ServiceType>
    [[nodiscard]] EventSinkT<EventVariant> getServiceSink() const noexcept
    {
        auto* service = getService<ServiceType>();
        if (service) {
            if constexpr (requires { service->sink(); }) {
                return service->sink();
            }
        }
        return EventSinkT<EventVariant>{};
    }

    /// @brief Post an event directly to a target service.
    /// @tparam TargetService Target service type to send the event to.
    /// @tparam EventType Event type to send.
    /// @param event Event instance to post.
    /// @param priority Priority level.
    /// @return true if event was posted to the target service; false if target service was not found or has no sink.
    template <typename TargetService, typename EventType>
    bool sendToService(EventType&& event, EventPriority priority = EventPriority::Normal) const
    {
        auto sinkHandle = getServiceSink<TargetService>();
        if (sinkHandle) {
            sinkHandle.post(std::forward<EventType>(event), priority);
            return true;
        }
        return false;
    }

private:
    EventSinkT<EventVariant> _eventSink;
    void* _registryPtr = nullptr;
    GetServiceFn _getServiceFn = nullptr;
};

/// @brief Default ServiceContext alias using DefaultEvents.
using ServiceContext = BasicServiceContext<DefaultEvents>;

/// @brief Templated ServiceContext alias for custom event variant.
template <typename EventVariant = DefaultEvents>
using ServiceContextT = BasicServiceContext<EventVariant>;

} // namespace corium

// <<< End: corium/ServiceContext.hpp

namespace corium {

namespace internal {

template <typename T, typename Context>
concept HasSetContext = requires(T& t, Context ctx) {
    t.setContext(ctx);
};

template <typename T>
concept HasInitialize = requires(T& t) {
    t.initialize();
};

template <typename T>
concept HasRunToken = requires(T& t, std::stop_token st) {
    t.run(st);
};

template <typename T>
concept HasStart = requires(T& t) {
    t.start();
};

template <typename T>
concept HasStop = requires(T& t) {
    t.stop();
};

template <typename T>
concept HasJoin = requires(T& t) {
    t.join();
};

} // namespace internal

/// @ingroup core
/// @brief Non-allocating ServiceRegistry storing service handles in a fixed stack/static array.
/// Zero heap allocations, zero vtables/RTTI.
/// @tparam MaxServices Maximum number of background services allowed per registry (default 8).
/// @tparam EventVariant Supported event variant type list.
template <size_t MaxServices = 8, typename EventVariant = DefaultEvents>
class BasicServiceRegistry {
public:
    using ServiceContextType = ServiceContextT<EventVariant>;

    /// @brief Non-allocating type-erased handle for static background services.
    struct ServiceHandle {
        void* instance = nullptr;
        internal::TypeIdPtr typeId = nullptr;
        void (*initFn)(void* inst, ServiceContextType ctx) = nullptr;
        void (*startFn)(void* inst) = nullptr;
        void (*stopFn)(void* inst) noexcept = nullptr;
        void (*joinFn)(void* inst) noexcept = nullptr;
    };

    BasicServiceRegistry() = default;

    /// @brief Register a background service instance by reference.
    /// @tparam ServiceType Type of the background service.
    /// @param serviceInstance Reference to service instance.
    /// @return true if service was registered successfully; false if registry is full.
    template <typename ServiceType>
    bool registerService(ServiceType& serviceInstance)
    {
        if (_count >= MaxServices) {
            return false;
        }
        _services[_count] = ServiceHandle{
            &serviceInstance,
            internal::getTypeId<ServiceType>(),
            [](void* ptr, ServiceContextType ctx) {
                auto* s = static_cast<ServiceType*>(ptr);
                if constexpr (internal::HasSetContext<ServiceType, ServiceContextType>) { s->setContext(ctx); }
                if constexpr (internal::HasInitialize<ServiceType>) { s->initialize(); }
            },
            [](void* ptr) {
                auto* s = static_cast<ServiceType*>(ptr);
                if constexpr (internal::HasRunToken<ServiceType>) {
                    s->startThread(s);
                } else if constexpr (internal::HasStart<ServiceType>) {
                    s->start();
                }
            },
            [](void* ptr) noexcept {
                auto* s = static_cast<ServiceType*>(ptr);
                if constexpr (internal::HasStop<ServiceType>) { s->stop(); }
            },
            [](void* ptr) noexcept {
                auto* s = static_cast<ServiceType*>(ptr);
                if constexpr (internal::HasJoin<ServiceType>) { s->join(); }
            }
        };
        _count++;
        return true;
    }

    /// @brief Retrieve a registered service instance by static type ID.
    [[nodiscard]] void* getServiceById(internal::TypeIdPtr typeId) const noexcept
    {
        for (size_t i = 0; i < _count; ++i) {
            if (_services[i].typeId == typeId) {
                return _services[i].instance;
            }
        }
        return nullptr;
    }

    /// @brief Retrieve a registered service instance by concrete type.
    /// @tparam ServiceType Type of the background service.
    /// @return Pointer to registered ServiceType instance, or nullptr if not found.
    template <typename ServiceType>
    [[nodiscard]] ServiceType* getService() const noexcept
    {
        return static_cast<ServiceType*>(getServiceById(internal::getTypeId<ServiceType>()));
    }

    /// @brief Retrieve event sink handle of a target registered service if available.
    /// @tparam ServiceType Type of the target background service.
    /// @return EventSinkT handle targeting the service's incoming queue, or empty handle if unavailable.
    template <typename ServiceType>
    [[nodiscard]] EventSinkT<EventVariant> getServiceSink() const noexcept
    {
        auto* service = getService<ServiceType>();
        if (service) {
            if constexpr (requires { service->sink(); }) {
                return service->sink();
            }
        }
        return EventSinkT<EventVariant>{};
    }

    /// @brief Initialize and launch all registered background service jthreads.
    void initialize(ServiceContextType ctx)
    {
        ctx.setRegistry(const_cast<BasicServiceRegistry*>(this), [](void* regPtr, internal::TypeIdPtr typeId) -> void* {
            return static_cast<BasicServiceRegistry*>(regPtr)->getServiceById(typeId);
        });

        for (size_t i = 0; i < _count; ++i) {
            if (_services[i].initFn) {
                _services[i].initFn(_services[i].instance, ctx);
            }
            if (_services[i].startFn) {
                _services[i].startFn(_services[i].instance);
            }
        }
    }

    /// @brief Stop and join all registered background service threads.
    void shutdown() noexcept
    {
        for (size_t i = 0; i < _count; ++i) {
            if (_services[i].stopFn) {
                _services[i].stopFn(_services[i].instance);
            }
        }
        for (size_t i = 0; i < _count; ++i) {
            if (_services[i].joinFn) {
                _services[i].joinFn(_services[i].instance);
            }
        }
        _count = 0;
    }

    /// @brief Access number of registered services.
    [[nodiscard]] size_t size() const noexcept
    {
        return _count;
    }

private:
    std::array<ServiceHandle, MaxServices> _services{};
    size_t _count = 0;
};

/// @brief Default ServiceRegistry alias using MaxServices=8 and DefaultEvents.
using ServiceRegistry = BasicServiceRegistry<8, DefaultEvents>;

/// @brief Templated ServiceRegistry alias for custom capacity and event variant.
template <size_t MaxServices = 8, typename EventVariant = DefaultEvents>
using ServiceRegistryT = BasicServiceRegistry<MaxServices, EventVariant>;

} // namespace corium

// <<< End: corium/ServiceRegistry.hpp

#include <utility>

namespace corium {

// Forward declaration of BasicRuntime for friendship
template <
    typename EventVariant,
    typename QueuePolicy,
    typename SignalPolicy,
    typename StoragePolicy,
    typename OverflowPolicy,
    typename TimerStoragePolicy,
    typename ProfilerPolicy
>
class BasicRuntime;

/// @ingroup core
/// @brief Static CRTP base class for applications managed by Corium Runtime.
/// Subclass Application<Derived> or Application<Derived, EventVariant, MaxServices> for zero-vtable compile-time static dispatch.
/// All framework operations and handlers are protected for clean encapsulation within the derived application.
/// @tparam Derived Subclass type implementing lifecycle hooks (onRegisterHandlers, onInitialize, onShutdown, onConfigureServices).
/// @tparam EventVariantOrBus Event variant type list or EventBus type (defaults to DefaultEvents).
/// @tparam MaxServices Maximum number of background services that can be registered (defaults to 8).
template <typename Derived, typename EventVariantOrBus = DefaultEvents, std::size_t MaxServices = 8>
class Application {
public:
    using EventVariant = internal::extract_event_variant_t<EventVariantOrBus>;
    using ServiceRegistryType = BasicServiceRegistry<MaxServices, EventVariant>;
    using ContextType = ApplicationContext<EventVariant>;

    Application() = default;
    ~Application()
    {
        shutdownServices();
        _context.detachFromRuntime();
    }

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

protected:
    /// @brief Register event handler with automatic event type deduction from callable signature.
    template <typename Handler>
    bool on(Handler&& handler)
    {
        return _context.registerHandler(std::forward<Handler>(handler));
    }

    /// @brief Register a filtered event handler executed only when predicate evaluates to true.
    /// @tparam Filter Callable returning bool when passed the event.
    /// @tparam Handler Callable taking const EventType&.
    template <typename Filter, typename Handler>
        requires (std::is_invocable_r_v<bool, Filter&, const callable_event_type_t<Handler>&>)
    bool on(Filter&& filter, Handler&& handler)
    {
        return _context.registerFilteredHandler(std::forward<Filter>(filter), std::forward<Handler>(handler));
    }

    /// @brief Access event sink handle.
    [[nodiscard]] EventSinkT<EventVariant> eventSink()
    {
        return _context.eventSink();
    }

    /// @brief Schedule a single-shot delayed event.
    template <typename Rep, typename Period>
    TimerId postDelayed(EventVariant event, const std::chrono::duration<Rep, Period>& delay, EventPriority priority = EventPriority::Normal)
    {
        return _context.scheduleDelayed(std::move(event), delay, priority);
    }

    /// @brief Schedule a recurring periodic event.
    template <typename Rep, typename Period>
    TimerId postPeriodic(EventVariant event, const std::chrono::duration<Rep, Period>& interval, EventPriority priority = EventPriority::Normal)
    {
        return _context.schedulePeriodic(std::move(event), interval, priority);
    }

    /// @brief Cancel an active timer.
    bool cancelTimer(TimerId id) noexcept
    {
        return _context.cancelTimer(id);
    }

    /// @brief Request graceful runtime shutdown.
    void requestQuit()
    {
        _context.requestQuit();
    }

    /// @brief Access background service registry.
    [[nodiscard]] ServiceRegistryType& services() noexcept
    {
        return _serviceRegistry;
    }

    /// @brief Access const background service registry.
    [[nodiscard]] const ServiceRegistryType& services() const noexcept
    {
        return _serviceRegistry;
    }

    /// @brief Retrieve registered background service by concrete type.
    template <typename ServiceType>
    [[nodiscard]] ServiceType* getService() const noexcept
    {
        return _serviceRegistry.template getService<ServiceType>();
    }

private:
    template <
        typename EV,
        typename QP,
        typename SP,
        typename STP,
        typename OP,
        typename TP,
        typename PP
    >
    friend class BasicRuntime;

    template <typename Registry>
    void configureServices(Registry& registry)
    {
        if constexpr (requires(Derived& d, Registry& r) { d.onConfigureServices(r); }) {
            static_cast<Derived*>(this)->onConfigureServices(registry);
        }
    }

    void initializeServices(EventSinkT<EventVariant> sink)
    {
        configureServices(_serviceRegistry);
        _serviceRegistry.initialize(BasicServiceContext<EventVariant>{sink});
    }

    void shutdownServices() noexcept
    {
        _serviceRegistry.shutdown();
    }

    void registerHandlers()
    {
        if constexpr (requires(Derived& d) { d.onRegisterHandlers(); }) {
            static_cast<Derived*>(this)->onRegisterHandlers();
        }
    }

    void initialize()
    {
        if constexpr (requires(Derived& d) { d.onInitialize(); }) {
            static_cast<Derived*>(this)->onInitialize();
        }
    }

    void shutdown()
    {
        if constexpr (requires(Derived& d) { d.onShutdown(); }) {
            static_cast<Derived*>(this)->onShutdown();
        }
    }

    void setContext(ApplicationContext<EventVariant> context)
    {
        _context = context;
        if constexpr (requires(Derived& d, ApplicationContext<EventVariant> c) { d.onSetContext(c); }) {
            static_cast<Derived*>(this)->onSetContext(context);
        }
    }

    void resetContext() noexcept
    {
        _context.reset();
    }

    ApplicationContext<EventVariant> _context;
    ServiceRegistryType _serviceRegistry;
};

} // namespace corium

// <<< End: corium/Application.hpp

// >>> Begin: corium/Service.hpp
/**
 * @file Service.hpp
 * @ingroup core
 * @brief Lightweight thread-safe background service interface.
 */



#include <cstddef>
#include <utility>

namespace corium {

/// @ingroup core
/// @brief Non-allocating base class for synchronous or thread-agnostic services.
/// Provides event producing (posting to main EventBus) and event consuming (receiving events into dedicated incoming bus).
/// Does NOT own a thread. Zero heap allocations, zero vtables/RTTI.
/// @tparam EventVariantType Supported event variant type list.
/// @tparam QueuePolicy Strategy for queueing incoming events (bounded lock-free MPSC).
/// @tparam SignalPolicy Strategy for signaling (CallbackSignalPolicy default).
/// @tparam StoragePolicy Strategy for compile-time handler capacity and delegate storage.
/// @tparam OverflowPolicy Strategy for queue overflow handling.
template <
    typename EventVariantType = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariantType, 1024>,
    typename SignalPolicy = CallbackSignalPolicy,
    typename StoragePolicy = DefaultStoragePolicy,
    typename OverflowPolicy = DropNewestOverflowPolicy
>
class Service {
public:
    using EventVariant = EventVariantType;
    using IncomingBus = BasicEventBus<EventVariant, QueuePolicy, SignalPolicy, StoragePolicy, OverflowPolicy>;

    Service() = default;

    explicit Service(ServiceContextT<EventVariant> context)
        : _context(context)
    {}

    ~Service() = default;

    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

    Service(Service&&) noexcept = default;
    Service& operator=(Service&&) noexcept = default;

    void setContext(ServiceContextT<EventVariant> context) noexcept
    {
        _context = context;
    }

    /// @brief Get an EventSink handle targeting this service's incoming event queue.
    [[nodiscard]] EventSinkT<EventVariant> sink() noexcept
    {
        return _incomingBus.sink();
    }

    /// @brief Get an EventSink handle targeting the main application event queue.
    [[nodiscard]] EventSinkT<EventVariant> mainSink() const noexcept
    {
        return _context.mainSink();
    }

    /// @brief Register an event handler for incoming events with explicit event type parameter.
    template <typename EventType, typename Handler>
    bool registerHandler(Handler&& handler)
    {
        return _incomingBus.template registerHandler<EventType>(std::forward<Handler>(handler));
    }

    /// @brief Register an event handler for incoming events with automatic event type deduction.
    template <typename Handler>
    bool on(Handler&& handler)
    {
        return _incomingBus.registerHandler(std::forward<Handler>(handler));
    }

    /// @brief Process a single incoming event from the service queue.
    /// @return true if an event was popped and dispatched; false if queue was empty.
    bool processOne()
    {
        return _incomingBus.processOne();
    }

    /// @brief Pump all pending incoming events from the queue until empty.
    /// @return Number of events processed.
    std::size_t pump()
    {
        std::size_t processed = 0;
        while (_incomingBus.processOne()) {
            processed++;
        }
        return processed;
    }

    /// @brief Pump up to maxEvents pending incoming events from the queue.
    /// @param maxEvents Maximum number of events to process.
    /// @return Number of events processed.
    std::size_t pump(std::size_t maxEvents)
    {
        std::size_t processed = 0;
        while (processed < maxEvents && _incomingBus.processOne()) {
            processed++;
        }
        return processed;
    }

protected:
    [[nodiscard]] ServiceContextT<EventVariant>& context() noexcept { return _context; }
    [[nodiscard]] const ServiceContextT<EventVariant>& context() const noexcept { return _context; }

    /// @brief Post an event into the main application event queue.
    template <typename EventType>
    void post(EventType&& event, EventPriority priority = EventPriority::Normal) const
    {
        _context.mainSink().post(std::forward<EventType>(event), priority);
    }

    /// @brief Post a high-priority event into the main application event queue.
    template <typename EventType>
    void postHighPriority(EventType&& event) const
    {
        _context.mainSink().postHighPriority(std::forward<EventType>(event));
    }

    /// @brief Post an event directly to another registered service.
    template <typename TargetService, typename EventType>
    bool sendToService(EventType&& event, EventPriority priority = EventPriority::Normal) const
    {
        return _context.template sendToService<TargetService>(std::forward<EventType>(event), priority);
    }

    [[nodiscard]] IncomingBus& incomingBus() noexcept { return _incomingBus; }
    [[nodiscard]] const IncomingBus& incomingBus() const noexcept { return _incomingBus; }

private:
    ServiceContextT<EventVariant> _context;
    [[no_unique_address]] IncomingBus _incomingBus;
};

/// @brief Zero-overhead Service alias for pure producer services (no incoming event queue/reactor allocations).
/// @tparam EventVariant Supported event variant type list.
template <typename EventVariant = DefaultEvents>
using ProducerService = Service<
    EventVariant,
    NoQueuePolicy<EventVariant>,
    NoSignalPolicy,
    ZeroStoragePolicy
>;

/// @brief Explicit Service alias for consumer services with configurable incoming event queue capacity.
/// @tparam EventVariant Supported event variant type list.
/// @tparam Capacity Incoming ring buffer event capacity.
template <typename EventVariant = DefaultEvents, std::size_t Capacity = 64>
using ConsumerService = Service<
    EventVariant,
    BoundedMpscQueuePolicy<EventVariant, Capacity>,
    CallbackSignalPolicy,
    DefaultStoragePolicy
>;

} // namespace corium

// <<< End: corium/Service.hpp

// >>> Begin: corium/BackgroundService.hpp
/**
 * @file BackgroundService.hpp
 * @ingroup core
 * @brief Managed worker thread using C++20 std::jthread and std::stop_token.
 */



#include <chrono>
#include <exception>
#include <stop_token>
#include <thread>

namespace corium {

/// @ingroup core
/// @brief Multi-threaded background worker service owning a dedicated std::jthread.
/// Integrates incoming event queue with background worker execution and cooperative shutdown via std::stop_token.
/// Zero heap allocations, zero vtables/RTTI.
/// @tparam EventVariantType Supported event variant type list.
/// @tparam QueuePolicy Strategy for queueing incoming events (bounded lock-free MPSC).
/// @tparam SignalPolicy Strategy for signaling (CallbackSignalPolicy default).
/// @tparam StoragePolicy Strategy for compile-time handler capacity and delegate storage.
/// @tparam OverflowPolicy Strategy for queue overflow handling.
template <
    typename EventVariantType = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariantType, 1024>,
    typename SignalPolicy = CallbackSignalPolicy,
    typename StoragePolicy = DefaultStoragePolicy,
    typename OverflowPolicy = DropNewestOverflowPolicy
>
class BackgroundService : public Service<EventVariantType, QueuePolicy, SignalPolicy, StoragePolicy, OverflowPolicy> {
public:
    using Base = Service<EventVariantType, QueuePolicy, SignalPolicy, StoragePolicy, OverflowPolicy>;
    using EventVariant = typename Base::EventVariant;

    BackgroundService() = default;

    explicit BackgroundService(ServiceContextT<EventVariant> context)
        : Base(context)
    {}

    ~BackgroundService()
    {
        stop();
        join();
    }

    BackgroundService(const BackgroundService&) = delete;
    BackgroundService& operator=(const BackgroundService&) = delete;

    BackgroundService(BackgroundService&&) noexcept = default;
    BackgroundService& operator=(BackgroundService&&) noexcept = default;

    /// @brief Wait for incoming events or timeout, then pump all available incoming events.
    /// Safe for use inside worker thread run(std::stop_token).
    /// @tparam Rep Duration representation type.
    /// @tparam Period Duration period type.
    /// @param stopToken std::stop_token from run(stopToken).
    /// @param timeout Maximum duration to wait if queue is empty.
    /// @return Number of events processed.
    template <typename Rep, typename Period>
    std::size_t waitAndPump(const std::stop_token& stopToken, const std::chrono::duration<Rep, Period>& timeout)
    {
        if (this->incomingBus().empty() && !stopToken.stop_requested()) {
            this->incomingBus().signalPolicy().wait_for(timeout);
        }

        std::size_t processed = 0;
        while (!stopToken.stop_requested()) {
            if (!this->incomingBus().processOne()) {
                break;
            }
            processed++;
        }
        return processed;
    }

    /// @brief Start execution loop on dedicated std::jthread.
    template <typename Derived>
    void startThread(Derived* derived)
    {
        _thread = std::jthread([this, derived](std::stop_token stopToken) {
#if __cpp_exceptions
            using EvVariant = EventVariantType;
            try {
                derived->run(stopToken);
            } catch (const std::exception& e) {
                if constexpr (requires { derived->onError(std::current_exception()); }) {
                    derived->onError(std::current_exception());
                } else if constexpr (requires { derived->onError(e.what()); }) {
                    derived->onError(e.what());
                }
                if constexpr (has_variant_type_v<ErrorEvent, EvVariant>) {
                    this->postHighPriority(ErrorEvent{1, reinterpret_cast<uintptr_t>(e.what())});
                }
            } catch (...) {
                if constexpr (requires { derived->onError(std::current_exception()); }) {
                    derived->onError(std::current_exception());
                }
                if constexpr (has_variant_type_v<ErrorEvent, EvVariant>) {
                    this->postHighPriority(ErrorEvent{1, 0});
                }
            }
#else
            derived->run(stopToken);
#endif
        });
    }

    /// @brief Request cancellation on worker std::jthread.
    void stop() noexcept
    {
        if (_thread.joinable()) {
            _thread.request_stop();
        }
    }

    /// @brief Join background std::jthread cleanly.
    void join() noexcept
    {
        if (_thread.joinable()) {
            _thread.join();
        }
    }

private:
    std::jthread _thread;
};

/// @brief Zero-overhead BackgroundService alias for producer worker threads (zero incoming queue/reactor footprint).
/// @tparam EventVariant Supported event variant type list.
template <typename EventVariant = DefaultEvents>
using ProducerBackgroundService = BackgroundService<
    EventVariant,
    NoQueuePolicy<EventVariant>,
    NoSignalPolicy,
    ZeroStoragePolicy
>;

/// @brief BackgroundService alias for consumer worker threads with configurable queue capacity.
/// @tparam EventVariant Supported event variant type list.
/// @tparam Capacity Incoming ring buffer event capacity.
template <typename EventVariant = DefaultEvents, std::size_t Capacity = 64>
using ConsumerBackgroundService = BackgroundService<
    EventVariant,
    BoundedMpscQueuePolicy<EventVariant, Capacity>,
    CallbackSignalPolicy,
    DefaultStoragePolicy
>;

} // namespace corium

// <<< End: corium/BackgroundService.hpp

// >>> Begin: corium/policies/Policies.hpp
/**
 * @file Policies.hpp
 * @ingroup policies
 * @brief Umbrella header for compile-time runtime strategy policies.
 */



// <<< End: corium/policies/Policies.hpp

// >>> Begin: corium/logging/logging.hpp
/**
 * @file logging.hpp
 * @ingroup logging
 * @brief Umbrella header for the zero-heap structured logging framework.
 */



// >>> Begin: corium/logging/LogLevel.hpp
/**
 * @file LogLevel.hpp
 * @ingroup logging
 * @brief Log severity enumeration and ANSI color formatting utilities.
 */


#include <cstdint>

namespace corium::logging {

/// @brief Logging severity levels.
enum class LogLevel : uint8_t {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off
};

/// @brief Convert LogLevel enum to string representation.
constexpr const char* logLevelToString(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Trace:    return "TRACE";
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO";
        case LogLevel::Warn:     return "WARN";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRITICAL";
        case LogLevel::Off:      return "OFF";
    }
    return "UNKNOWN";
}

/// @brief Get ANSI color escape code for LogLevel.
constexpr const char* logLevelToColor(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Trace:    return "\033[36m"; // Cyan
        case LogLevel::Debug:    return "\033[34m"; // Blue
        case LogLevel::Info:     return "\033[32m"; // Green
        case LogLevel::Warn:     return "\033[33m"; // Yellow
        case LogLevel::Error:    return "\033[31m"; // Red
        case LogLevel::Critical: return "\033[35m"; // Magenta
        case LogLevel::Off:      return "\033[0m";  // Reset
    }
    return "\033[0m";
}

/// @brief ANSI reset color escape code.
constexpr const char* LOG_COLOR_RESET = "\033[0m";

} // namespace corium::logging

// <<< End: corium/logging/LogLevel.hpp

// >>> Begin: corium/logging/LogEvent.hpp
/**
 * @file LogEvent.hpp
 * @ingroup logging
 * @brief Zero-heap fixed-capacity inline buffer log event record.
 */


#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>


namespace corium::logging {

/// @brief Zero-heap log event carrying fixed-size inline message buffer across MPSC event bus.
/// @tparam MaxMessageLength Maximum character capacity for inline log message.
template <std::size_t MaxMessageLength = 256>
struct LogEventT {
    LogLevel level = LogLevel::Info;
    std::array<char, MaxMessageLength> message{};
    std::size_t length = 0;
    const char* category = "App";
    uint64_t timestampNs = 0;

    constexpr LogEventT() = default;

    /// @brief Create log event with inline formatted string.
    constexpr LogEventT(LogLevel lvl, std::string_view msg, const char* cat = "App", uint64_t ts = 0) noexcept
        : level(lvl), category(cat ? cat : "App"), timestampNs(ts)
    {
        setMessage(msg);
    }

    /// @brief Set inline message buffer safely up to MaxMessageLength - 1.
    constexpr void setMessage(std::string_view msg) noexcept
    {
        std::size_t copyLen = msg.length() < (MaxMessageLength - 1) ? msg.length() : (MaxMessageLength - 1);
        for (std::size_t i = 0; i < copyLen; ++i) {
            message[i] = msg[i];
        }
        message[copyLen] = '\0';
        length = copyLen;
    }

    /// @brief Access message as std::string_view.
    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view(message.data(), length);
    }
};

/// @brief Default LogEvent alias with 256-byte inline buffer.
using LogEvent = LogEventT<256>;

} // namespace corium::logging

// <<< End: corium/logging/LogEvent.hpp

// >>> Begin: corium/logging/sinks/ConsoleLogSink.hpp
/**
 * @file ConsoleLogSink.hpp
 * @ingroup logging
 * @brief Standard console output log sink with ANSI color support.
 */


#include <iostream>

namespace corium::logging::sinks {

/// @brief Standard console output log sink with optional ANSI color codes.
class ConsoleLogSink {
public:
    explicit ConsoleLogSink(bool useColors = true) noexcept
        : _useColors(useColors)
    {}

    /// @brief Write log event to std::cout or std::cerr.
    template <std::size_t N>
    void write(const LogEventT<N>& event) const
    {
        std::ostream& os = (event.level >= LogLevel::Error) ? std::cerr : std::cout;

        if (_useColors) {
            os << logLevelToColor(event.level)
               << "[" << logLevelToString(event.level) << "]"
               << LOG_COLOR_RESET
               << " [" << event.category << "] "
               << event.view() << "\n";
        } else {
            os << "[" << logLevelToString(event.level) << "]"
               << " [" << event.category << "] "
               << event.view() << "\n";
        }
        os << std::flush;
    }

    /// @brief Enable or disable ANSI color formatting.
    void setColorsEnabled(bool enable) noexcept
    {
        _useColors = enable;
    }

    [[nodiscard]] bool colorsEnabled() const noexcept
    {
        return _useColors;
    }

private:
    bool _useColors = true;
};

} // namespace corium::logging::sinks

// <<< End: corium/logging/sinks/ConsoleLogSink.hpp

// >>> Begin: corium/logging/sinks/FileLogSink.hpp
/**
 * @file FileLogSink.hpp
 * @ingroup logging
 * @brief Synchronous append-only file logging sink.
 */


#include <cstdio>

namespace corium::logging::sinks {

/// @brief File log sink appending formatted log entries to a designated file.
class FileLogSink {
public:
    FileLogSink() = default;

    explicit FileLogSink(const char* filePath)
    {
        open(filePath);
    }

    ~FileLogSink()
    {
        close();
    }

    FileLogSink(const FileLogSink&) = delete;
    FileLogSink& operator=(const FileLogSink&) = delete;

    FileLogSink(FileLogSink&& rhs) noexcept
    {
        _file = rhs._file;
        rhs._file = nullptr;
    }

    FileLogSink& operator=(FileLogSink&& rhs) noexcept
    {
        if (this != &rhs) {
            close();
            _file = rhs._file;
            rhs._file = nullptr;
        }
        return *this;
    }

    /// @brief Open target log file in append mode.
    bool open(const char* filePath)
    {
        close();
        if (filePath && filePath[0] != '\0') {
#if defined(_MSC_VER)
            ::fopen_s(&_file, filePath, "a");
#else
            _file = std::fopen(filePath, "a");
#endif
        }
        return _file != nullptr;
    }

    /// @brief Close target log file.
    void close() noexcept
    {
        if (_file) {
            (void)std::fclose(_file);
            _file = nullptr;
        }
    }

    /// @brief Write log event entry to file.
    template <std::size_t N>
    void write(const LogEventT<N>& event) const
    {
        if (_file) {
            (void)std::fprintf(_file, "[%s] [%s] %.*s\n",
                               logLevelToString(event.level),
                               event.category,
                               static_cast<int>(event.length),
                               event.message.data());
            (void)std::fflush(_file);
        }
    }

    [[nodiscard]] bool isOpen() const noexcept
    {
        return _file != nullptr;
    }

private:
    std::FILE* _file = nullptr;
};

} // namespace corium::logging::sinks

// <<< End: corium/logging/sinks/FileLogSink.hpp

// >>> Begin: corium/logging/sinks/JsonLogSink.hpp
/**
 * @file JsonLogSink.hpp
 * @ingroup logging
 * @brief Structured JSON Lines (NDJSON) output log sink for observability.
 */


#include <ostream>
#include <string_view>

namespace corium::logging::sinks {

/// @ingroup logging
/// @brief Structured JSON Lines (NDJSON) output log sink.
/// Formats LogEvent records into JSON objects for structured telemetry and ingest (ELK, Datadog, Grafana Loki).
class JsonLogSink {
public:
    explicit JsonLogSink(std::ostream& outputStream) noexcept
        : _os(&outputStream)
    {}

    /// @brief Serialize log event as a single-line JSON string.
    template <std::size_t N>
    void write(const LogEventT<N>& event) const
    {
        if (!_os) return;

        *_os << "{\"timestamp_ns\":" << event.timestampNs
             << ",\"level\":\"" << logLevelToString(event.level) << "\""
             << ",\"category\":\"" << event.category << "\""
             << ",\"message\":\"";

        // Escape JSON string characters
        std::string_view msg = event.view();
        for (char c : msg) {
            switch (c) {
                case '"':  *_os << "\\\""; break;
                case '\\': *_os << "\\\\"; break;
                case '\b': *_os << "\\b";  break;
                case '\f': *_os << "\\f";  break;
                case '\n': *_os << "\\n";  break;
                case '\r': *_os << "\\r";  break;
                case '\t': *_os << "\\t";  break;
                default:   *_os << c;       break;
            }
        }

        *_os << "\"}\n" << std::flush;
    }

private:
    std::ostream* _os{nullptr};
};

} // namespace corium::logging::sinks

// <<< End: corium/logging/sinks/JsonLogSink.hpp

// >>> Begin: corium/logging/sinks/NullLogSink.hpp
/**
 * @file NullLogSink.hpp
 * @ingroup logging
 * @brief No-op compile-time disabled log sink for zero overhead.
 */



namespace corium::logging::sinks {

/// @brief No-op log sink for zero-cost logging in benchmarks or production headless mode.
class NullLogSink {
public:
    NullLogSink() = default;

    template <std::size_t N>
    void write(const LogEventT<N>& event) const noexcept
    {
        (void)event;
    }
};

} // namespace corium::logging::sinks

// <<< End: corium/logging/sinks/NullLogSink.hpp

// >>> Begin: corium/logging/Logger.hpp
/**
 * @file Logger.hpp
 * @ingroup logging
 * @brief Static logger frontend with compile-time formatting and severity filtering.
 */


#include <cstdio>
#include <utility>


namespace corium::logging {

/// @ingroup logging
/// @brief Static compile-time logger wrapper managing severity level filtering and zero-heap string formatting.
/// @tparam LogSink Log sink backend handling actual log entry output (e.g. ConsoleLogSink, FileLogSink, NullLogSink).
/// @tparam MaxMessageSize Maximum character length for formatted log messages.
template <typename LogSink = sinks::ConsoleLogSink, std::size_t MaxMessageSize = 256>
class LoggerT {
public:
    using EventType = LogEventT<MaxMessageSize>;

    explicit LoggerT(const char* category = "App", LogLevel minLevel = LogLevel::Info, LogSink sink = LogSink{})
        : _category(category ? category : "App"), _minLevel(minLevel), _sink(std::move(sink))
    {}

    /// @brief Set minimum log severity level.
    void setMinLevel(LogLevel level) noexcept
    {
        _minLevel = level;
    }

    /// @brief Get minimum log severity level.
    [[nodiscard]] LogLevel minLevel() const noexcept
    {
        return _minLevel;
    }

    /// @brief Set category string tag.
    void setCategory(const char* category) noexcept
    {
        _category = category ? category : "App";
    }

    /// @brief Get category tag.
    [[nodiscard]] const char* category() const noexcept
    {
        return _category;
    }

    /// @brief Access underlying log sink reference.
    [[nodiscard]] LogSink& sink() noexcept { return _sink; }
    [[nodiscard]] const LogSink& sink() const noexcept { return _sink; }

    /// @brief Log formatted message directly to sink if severity level meets minimum threshold.
    template <typename... Args>
    void log(LogLevel level, const char* fmt, Args&&... args) const
    {
        if (level < _minLevel || _minLevel == LogLevel::Off) {
            return;
        }

        EventType event{};
        event.level = level;
        event.category = _category;

        if constexpr (sizeof...(Args) == 0) {
            event.setMessage(fmt);
        } else {
            int written = std::snprintf(event.message.data(), MaxMessageSize, fmt, std::forward<Args>(args)...);
            if (written > 0) {
                event.length = static_cast<std::size_t>(written) < MaxMessageSize ? static_cast<std::size_t>(written) : (MaxMessageSize - 1);
            }
        }

        _sink.write(event);
    }

    /// @brief Post formatted LogEvent to a target Corium EventSink (lock-free MPSC event bus).
    template <typename TargetEventSink, typename... Args>
    void logToSink(TargetEventSink&& targetSink, LogLevel level, const char* fmt, Args&&... args) const
    {
        if (level < _minLevel || _minLevel == LogLevel::Off) {
            return;
        }

        EventType event{};
        event.level = level;
        event.category = _category;

        if constexpr (sizeof...(Args) == 0) {
            event.setMessage(fmt);
        } else {
            int written = std::snprintf(event.message.data(), MaxMessageSize, fmt, std::forward<Args>(args)...);
            if (written > 0) {
                event.length = static_cast<std::size_t>(written) < MaxMessageSize ? static_cast<std::size_t>(written) : (MaxMessageSize - 1);
            }
        }

        targetSink.post(std::move(event));
    }

    template <typename... Args>
    void trace(const char* fmt, Args&&... args) const { log(LogLevel::Trace, fmt, std::forward<Args>(args)...); }

    template <typename... Args>
    void debug(const char* fmt, Args&&... args) const { log(LogLevel::Debug, fmt, std::forward<Args>(args)...); }

    template <typename... Args>
    void info(const char* fmt, Args&&... args) const { log(LogLevel::Info, fmt, std::forward<Args>(args)...); }

    template <typename... Args>
    void warn(const char* fmt, Args&&... args) const { log(LogLevel::Warn, fmt, std::forward<Args>(args)...); }

    template <typename... Args>
    void error(const char* fmt, Args&&... args) const { log(LogLevel::Error, fmt, std::forward<Args>(args)...); }

    template <typename... Args>
    void critical(const char* fmt, Args&&... args) const { log(LogLevel::Critical, fmt, std::forward<Args>(args)...); }

private:
    const char* _category = "App";
    LogLevel _minLevel = LogLevel::Info;
    LogSink _sink{};
};

/// @brief Default Console Logger alias.
using ConsoleLogger = LoggerT<sinks::ConsoleLogSink>;

/// @brief Default File Logger alias.
using FileLogger = LoggerT<sinks::FileLogSink>;

/// @brief Default Null Logger alias.
using NullLogger = LoggerT<sinks::NullLogSink>;

} // namespace corium::logging

// <<< End: corium/logging/Logger.hpp

// >>> Begin: corium/logging/LogBackgroundService.hpp
/**
 * @file LogBackgroundService.hpp
 * @ingroup logging
 * @brief Asynchronous background worker service for flushing log events to disk.
 */


#include <chrono>
#include <stop_token>
#include <thread>
#include <utility>


namespace corium::logging {

/// @brief Asynchronous background logging service executing on a dedicated C++20 std::jthread.
/// Consumes/flushes log events or periodic status messages asynchronously without blocking the main event loop.
/// @tparam LogSink Log sink backend type (e.g. ConsoleLogSink, FileLogSink, NullLogSink).
/// @tparam EventVariant Event variant list supported by the runtime context.
/// @tparam MaxMessageSize Inline capacity size for log events.
template <typename LogSink = sinks::ConsoleLogSink, typename EventVariant = DefaultEvents, std::size_t MaxMessageSize = 256>
class LogBackgroundService : public BackgroundService<EventVariant> {
public:
    using LoggerType = LoggerT<LogSink, MaxMessageSize>;

    explicit LogBackgroundService(const char* category = "LogService", LogLevel minLevel = LogLevel::Info, LogSink sink = LogSink{})
        : _logger(category, minLevel, std::move(sink))
    {}

    /// @brief Access embedded logger instance.
    [[nodiscard]] LoggerType& logger() noexcept { return _logger; }
    [[nodiscard]] const LoggerType& logger() const noexcept { return _logger; }

    /// @brief Log background heartbeat or event message.
    template <typename... Args>
    void info(const char* fmt, Args&&... args) const
    {
        _logger.info(fmt, std::forward<Args>(args)...);
    }

    /// @brief Main background execution loop.
    void run(const std::stop_token& stopToken)
    {
        _logger.info("Asynchronous LogBackgroundService started.");
        while (!stopToken.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        _logger.info("Asynchronous LogBackgroundService stopping.");
    }

private:
    LoggerType _logger;
};

} // namespace corium::logging

// <<< End: corium/logging/LogBackgroundService.hpp

// <<< End: corium/logging/logging.hpp

// >>> Begin: corium/embedded/embedded.hpp
/**
 * @file embedded.hpp
 * @ingroup embedded
 * @brief Umbrella header for embedded and RTOS integration primitives.
 */



// >>> Begin: corium/embedded/InterruptLock.hpp
/**
 * @file InterruptLock.hpp
 * @ingroup embedded
 * @brief Zero-overhead RAII critical section masking across ARM, ESP32, and host.
 */


#include <cstdint>

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#define CORIUM_HAS_ESP32_CRITICAL 1
#elif defined(__arm__) || defined(__thumb__)
#if __has_include(<cmsis_compiler.h>)
#include <cmsis_compiler.h>
#define CORIUM_HAS_ARM_CMSIS 1
#endif
#endif

namespace corium::embedded {

/// @ingroup embedded
/// @brief RAII interrupt locking helper disabling interrupts upon construction and restoring them upon destruction.
class InterruptLock {
public:
    InterruptLock() noexcept
    {
        lock();
    }

    ~InterruptLock() noexcept
    {
        unlock();
    }

    InterruptLock(const InterruptLock&) = delete;
    InterruptLock& operator=(const InterruptLock&) = delete;

    InterruptLock(InterruptLock&&) = delete;
    InterruptLock& operator=(InterruptLock&&) = delete;

    void lock() noexcept
    {
        if (!_locked) {
#if defined(CORIUM_HAS_ESP32_CRITICAL)
            portENTER_CRITICAL(&_mux);
#elif defined(CORIUM_HAS_ARM_CMSIS)
            _primask = __get_PRIMASK();
            __disable_irq();
#endif
            _locked = true;
        }
    }

    void unlock() noexcept
    {
        if (_locked) {
#if defined(CORIUM_HAS_ESP32_CRITICAL)
            portEXIT_CRITICAL(&_mux);
#elif defined(CORIUM_HAS_ARM_CMSIS)
            __set_PRIMASK(_primask);
#endif
            _locked = false;
        }
    }

    [[nodiscard]] bool isLocked() const noexcept
    {
        return _locked;
    }

private:
    bool _locked = false;
#if defined(CORIUM_HAS_ESP32_CRITICAL)
    static inline portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
#elif defined(CORIUM_HAS_ARM_CMSIS)
    uint32_t _primask = 0;
#endif
};

} // namespace corium::embedded

// <<< End: corium/embedded/InterruptLock.hpp

// >>> Begin: corium/embedded/IsrSink.hpp
/**
 * @file IsrSink.hpp
 * @ingroup embedded
 * @brief Safe non-blocking event posting handle for hardware interrupt service routines.
 */


#include <utility>

namespace corium::embedded {

/// @ingroup embedded
/// @brief Lightweight, zero-overhead wrapper around EventSink explicitly tailored for Hardware ISR handlers.
/// Guarantees zero heap allocation, lock-free execution, and noexcept exception safety.
/// @tparam EventSinkType Target underlying event sink type.
template <typename EventSinkType>
class IsrEventSink {
public:
    constexpr IsrEventSink() noexcept = default;

    constexpr explicit IsrEventSink(EventSinkType sink) noexcept
        : _sink(sink)
    {}

    /// @brief Post an event safely from hardware ISR context.
    /// @tparam Event Concrete event type.
    /// @param event Event instance to post.
    /// @param priority Priority level (High, Normal, Low).
    template <typename Event>
    void postFromIsr(Event&& event, EventPriority priority = EventPriority::Normal) noexcept
    {
        _sink.post(std::forward<Event>(event), priority);
    }

    /// @brief Post a high-priority emergency or hardware interrupt event from ISR context.
    /// @tparam Event Concrete event type.
    /// @param event Event instance to post.
    template <typename Event>
    void postHighPriorityFromIsr(Event&& event) noexcept
    {
        _sink.postHighPriority(std::forward<Event>(event));
    }

    /// @brief Post an event from ISR context.
    template <typename Event>
    void tryPostFromIsr(Event&& event, EventPriority priority = EventPriority::Normal) noexcept
    {
        _sink.post(std::forward<Event>(event), priority);
    }

    /// @brief Access underlying EventSink handle.
    [[nodiscard]] constexpr EventSinkType& sink() noexcept
    {
        return _sink;
    }

    [[nodiscard]] constexpr const EventSinkType& sink() const noexcept
    {
        return _sink;
    }

private:
    EventSinkType _sink{};
};

/// @brief Helper function to construct an IsrEventSink from an EventSink handle.
template <typename EventSinkType>
[[nodiscard]] constexpr auto makeIsrSink(EventSinkType sink) noexcept
{
    return IsrEventSink<EventSinkType>(sink);
}

} // namespace corium::embedded

// <<< End: corium/embedded/IsrSink.hpp

// >>> Begin: corium/embedded/FreeRtos.hpp
/**
 * @file FreeRtos.hpp
 * @ingroup embedded
 * @brief FreeRTOS ISR event sink and hardware context-switching helpers.
 */


#include <utility>

#if defined(FREERTOS) || defined(INC_FREERTOS_H) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#define CORIUM_HAS_FREERTOS_INCLUDES 1
#else
// Mock definitions for host build and desktop testing
using BaseType_t = long;
#define pdTRUE 1
#define pdFALSE 0
#define portYIELD_FROM_ISR(x) ((void)(x))
#endif

namespace corium::embedded {

/// @ingroup embedded
/// @brief FreeRTOS-specialized ISR sink managing RTOS task context switches.
/// @tparam EventSinkType Target underlying event sink.
template <typename EventSinkType>
class FreeRtosIsrSink {
public:
    constexpr FreeRtosIsrSink() noexcept = default;

    constexpr explicit FreeRtosIsrSink(EventSinkType sink) noexcept
        : _sink(sink)
    {}

    /// @brief Post event from FreeRTOS ISR and track if context switch is needed.
    /// @tparam Event Concrete event type.
    /// @param event Event instance.
    /// @param pxHigherPriorityTaskWoken Pointer to FreeRTOS task unblocking flag.
    /// @param priority Event priority level.
    template <typename Event>
    void postFromIsr(Event&& event, BaseType_t* pxHigherPriorityTaskWoken = nullptr, EventPriority priority = EventPriority::Normal) noexcept
    {
        _sink.post(std::forward<Event>(event), priority);
        if (pxHigherPriorityTaskWoken) {
            *pxHigherPriorityTaskWoken = pdTRUE;
        }
    }

    /// @brief Post high priority event from FreeRTOS ISR and track context switch.
    template <typename Event>
    void postHighPriorityFromIsr(Event&& event, BaseType_t* pxHigherPriorityTaskWoken = nullptr) noexcept
    {
        _sink.postHighPriority(std::forward<Event>(event));
        if (pxHigherPriorityTaskWoken) {
            *pxHigherPriorityTaskWoken = pdTRUE;
        }
    }

    /// @brief Yield to higher priority unblocked task if flag was set.
    static void yieldFromIsr(BaseType_t xHigherPriorityTaskWoken) noexcept
    {
        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }

    /// @brief Access underlying sink handle.
    [[nodiscard]] constexpr EventSinkType& sink() noexcept { return _sink; }
    [[nodiscard]] constexpr const EventSinkType& sink() const noexcept { return _sink; }

private:
    EventSinkType _sink{};
};

/// @brief Helper function to construct FreeRtosIsrSink from an event sink handle.
template <typename EventSinkType>
[[nodiscard]] constexpr auto makeFreeRtosIsrSink(EventSinkType sink) noexcept
{
    return FreeRtosIsrSink<EventSinkType>(sink);
}

} // namespace corium::embedded

// <<< End: corium/embedded/FreeRtos.hpp

// >>> Begin: corium/embedded/CanAdapter.hpp
/**
 * @file CanAdapter.hpp
 * @ingroup embedded
 * @brief Zero-heap CAN 2.0B and CAN-FD hardware ISR adapter for STM32 FDCAN, ESP32 TWAI, and CMSIS.
 */


#include <array>
#include <cstdint>
#include <span>


namespace corium::embedded {

/// @brief Standard CAN 2.0B classic frame structure (up to 8 bytes payload).
struct CanFrame {
    uint32_t id{0};               ///< 11-bit standard or 29-bit extended identifier
    uint8_t dlc{0};               ///< Data Length Code (0..8)
    bool isExtended{false};       ///< True if 29-bit extended ID
    bool isRtr{false};            ///< Remote Transmission Request flag
    std::array<uint8_t, 8> data{}; ///< Payload bytes

    [[nodiscard]] std::span<const uint8_t> payload() const noexcept {
        return {data.data(), static_cast<std::size_t>(dlc > 8 ? 8 : dlc)};
    }
};

/// @brief ISO 11898-1:2015 CAN-FD flexible data-rate frame structure (up to 64 bytes payload).
struct CanFdFrame {
    uint32_t id{0};                ///< 11-bit standard or 29-bit extended identifier
    uint8_t len{0};                ///< Data length in bytes (0..64)
    bool isExtended{false};        ///< True if 29-bit extended ID
    bool bitRateSwitch{false};     ///< BRS: Bit Rate Switch flag
    bool errorStateIndicator{false}; ///< ESI: Error State Indicator flag
    std::array<uint8_t, 64> data{}; ///< Payload bytes (up to 64B)

    [[nodiscard]] std::span<const uint8_t> payload() const noexcept {
        return {data.data(), static_cast<std::size_t>(len > 64 ? 64 : len)};
    }
};

/// @ingroup embedded
/// @brief Non-blocking hardware ISR adapter for CAN/CAN-FD controller interrupts.
/// Ingests hardware mailbox/FIFO frames inside ISR and posts into Corium lock-free ring buffer.
template <typename EventVariant>
class CanIsrAdapter {
public:
    explicit CanIsrAdapter(EventSinkT<EventVariant> sink) noexcept
        : _sink(sink)
    {}

    /// @brief Ingest standard CAN frame from hardware interrupt (e.g. STM32 CAN_RX_IRQHandler).
    /// @tparam TargetEvent User-defined event type constructible from CanFrame.
    /// @param frame Raw hardware CAN frame.
    /// @param priority Priority to assign (default Normal, High for emergency/E-Stop).
    template <typename TargetEvent = CanFrame>
    void postFromIsr(const CanFrame& frame, EventPriority priority = EventPriority::Normal) noexcept {
        if constexpr (std::is_same_v<TargetEvent, CanFrame>) {
            _sink.post(EventVariant{frame}, priority);
        } else {
            _sink.post(EventVariant{TargetEvent{frame}}, priority);
        }
    }

    /// @brief Ingest high-speed CAN-FD frame from hardware interrupt (e.g. STM32 FDCAN1_IT0_IRQHandler).
    /// @tparam TargetEvent User-defined event type constructible from CanFdFrame.
    /// @param frame Raw hardware CAN-FD frame.
    /// @param priority Priority to assign.
    template <typename TargetEvent = CanFdFrame>
    void postFromIsr(const CanFdFrame& frame, EventPriority priority = EventPriority::Normal) noexcept {
        if constexpr (std::is_same_v<TargetEvent, CanFdFrame>) {
            _sink.post(EventVariant{frame}, priority);
        } else {
            _sink.post(EventVariant{TargetEvent{frame}}, priority);
        }
    }

private:
    EventSinkT<EventVariant> _sink;
};

} // namespace corium::embedded

// <<< End: corium/embedded/CanAdapter.hpp

// >>> Begin: corium/embedded/DmaUartAdapter.hpp
/**
 * @file DmaUartAdapter.hpp
 * @ingroup embedded
 * @brief Zero-copy circular DMA UART receiver adapter for STM32, ESP32, and bare-metal UART ISRs.
 */


#include <array>
#include <cstddef>
#include <cstdint>
#include <span>


namespace corium::embedded {

/// @brief Statically-allocated circular DMA UART RX buffer.
/// @tparam BufferSize Circular buffer capacity in bytes (must be power of two).
template <std::size_t BufferSize = 512>
class DmaUartRxBuffer {
    static_assert((BufferSize & (BufferSize - 1)) == 0, "BufferSize must be a power of two.");

public:
    constexpr DmaUartRxBuffer() = default;

    /// @brief Direct pointer to raw memory buffer for DMA peripheral configuration (e.g. HAL_UART_Receive_DMA).
    [[nodiscard]] uint8_t* dmaBuffer() noexcept {
        return _buffer.data();
    }

    /// @brief Const pointer to raw memory buffer.
    [[nodiscard]] const uint8_t* dmaBuffer() const noexcept {
        return _buffer.data();
    }

    /// @brief Capacity of circular DMA buffer in bytes.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return BufferSize;
    }

    /// @brief Update tail position based on hardware DMA counter and extract available bytes.
    /// Typically invoked from UART Idle Line ISR or DMA Half/Full Transfer ISR.
    /// @param currentDmaWriteIndex Current hardware DMA write index (e.g. BufferSize - __HAL_DMA_GET_COUNTER(hdma)).
    /// @param onPacket Extracted byte span callback: void(std::span<const uint8_t> data).
    template <typename Callback>
    void processAvailableBytes(std::size_t currentDmaWriteIndex, Callback&& onPacket) {
        currentDmaWriteIndex = currentDmaWriteIndex & (BufferSize - 1);

        if (currentDmaWriteIndex == _tail) {
            return; // No new bytes received
        }

        if (currentDmaWriteIndex > _tail) {
            // Contiguous slice
            std::size_t len = currentDmaWriteIndex - _tail;
            onPacket(std::span<const uint8_t>(_buffer.data() + _tail, len));
            _tail = currentDmaWriteIndex;
        } else {
            // Wrapped around circular buffer
            std::size_t firstPart = BufferSize - _tail;
            if (firstPart > 0) {
                onPacket(std::span<const uint8_t>(_buffer.data() + _tail, firstPart));
            }
            if (currentDmaWriteIndex > 0) {
                onPacket(std::span<const uint8_t>(_buffer.data(), currentDmaWriteIndex));
            }
            _tail = currentDmaWriteIndex;
        }
    }

    /// @brief Reset circular tail index.
    void reset() noexcept {
        _tail = 0;
    }

    /// @brief Current tail index in the circular buffer.
    [[nodiscard]] std::size_t tail() const noexcept {
        return _tail;
    }

private:
    alignas(4) std::array<uint8_t, BufferSize> _buffer{};
    std::size_t _tail{0};
};

/// @ingroup embedded
/// @brief Non-blocking hardware ISR adapter for DMA UART controllers.
template <typename EventVariant, std::size_t BufferSize = 512>
class DmaUartAdapter {
public:
    explicit DmaUartAdapter(EventSinkT<EventVariant> sink) noexcept
        : _sink(sink)
    {}

    /// @brief Access underlying circular DMA RX buffer.
    [[nodiscard]] DmaUartRxBuffer<BufferSize>& rxBuffer() noexcept {
        return _rxBuffer;
    }

    /// @brief Access const circular DMA RX buffer.
    [[nodiscard]] const DmaUartRxBuffer<BufferSize>& rxBuffer() const noexcept {
        return _rxBuffer;
    }

    /// @brief Process newly received DMA bytes and post constructed event into sink.
    /// @tparam TargetEvent User event constructible from std::span<const uint8_t>.
    /// @param currentDmaWriteIndex Hardware DMA write pointer position.
    /// @param priority Priority to assign to generated events.
    template <typename TargetEvent>
    void processFromIsr(std::size_t currentDmaWriteIndex, EventPriority priority = EventPriority::Normal) {
        _rxBuffer.processAvailableBytes(currentDmaWriteIndex, [this, priority](std::span<const uint8_t> data) {
            _sink.post(EventVariant{TargetEvent{data}}, priority);
        });
    }

private:
    EventSinkT<EventVariant> _sink;
    DmaUartRxBuffer<BufferSize> _rxBuffer{};
};

} // namespace corium::embedded

// <<< End: corium/embedded/DmaUartAdapter.hpp

// >>> Begin: corium/embedded/SpiAdapter.hpp
/**
 * @file SpiAdapter.hpp
 * @ingroup embedded
 * @brief Zero-heap SPI hardware ISR and DMA completion adapter for embedded sensors (IMU, ADC, Flash).
 */


#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>


namespace corium::embedded {

/// @brief Fixed-capacity SPI transfer frame structure for zero-heap ISR and DMA ingestion.
/// @tparam MaxRxSize Maximum receive buffer size in bytes (default: 32).
template <size_t MaxRxSize = 32>
struct SpiFrame {
    uint8_t chipSelectId{0};       ///< Chip-select / device channel identifier
    uint16_t length{0};            ///< Number of valid received bytes
    uint8_t status{0};             ///< Hardware status flags (0: Success, 1: Error/Timeout)
    std::array<uint8_t, MaxRxSize> rxData{}; ///< Static receive buffer

    [[nodiscard]] std::span<const uint8_t> payload() const noexcept {
        return {rxData.data(), static_cast<size_t>(length > MaxRxSize ? MaxRxSize : length)};
    }
};

/// @ingroup embedded
/// @brief Non-blocking hardware ISR adapter for SPI controllers and DMA completion interrupts.
/// Ingests SPI frames from interrupt context and posts typed events into Corium lock-free ring buffer.
template <typename EventVariant>
class SpiIsrAdapter {
public:
    explicit SpiIsrAdapter(EventSinkT<EventVariant> sink) noexcept
        : m_sink(sink)
    {}

    /// @brief Post an SPI frame directly from ISR context into the event sink.
    /// @tparam TargetEvent User event type constructible from SpiFrame or SpiFrame itself.
    /// @tparam MaxRxSize Buffer capacity of the SPI frame.
    /// @param frame The completed SPI hardware frame.
    /// @param priority Priority to assign (default: Normal).
    template <typename TargetEvent = SpiFrame<32>, size_t MaxRxSize = 32>
    void postFromIsr(const SpiFrame<MaxRxSize>& frame, EventPriority priority = EventPriority::Normal) noexcept {
        if constexpr (std::is_same_v<TargetEvent, SpiFrame<MaxRxSize>>) {
            m_sink.post(EventVariant{frame}, priority);
        } else {
            m_sink.post(EventVariant{TargetEvent{frame}}, priority);
        }
    }

    /// @brief Ingest raw SPI RX bytes from a DMA buffer into a typed event and post into sink.
    /// @tparam TargetEvent User event type constructible from SpiFrame.
    /// @param csId Chip-select ID of the SPI slave.
    /// @param rxBytes Span of received bytes.
    /// @param status Hardware status code.
    /// @param priority Event priority.
    template <typename TargetEvent = SpiFrame<32>>
    void postBufferFromIsr(
        uint8_t csId,
        std::span<const uint8_t> rxBytes,
        uint8_t status = 0,
        EventPriority priority = EventPriority::Normal
    ) noexcept {
        SpiFrame<32> frame{};
        frame.chipSelectId = csId;
        frame.status = status;
        frame.length = static_cast<uint16_t>(rxBytes.size() > 32 ? 32 : rxBytes.size());
        std::memcpy(frame.rxData.data(), rxBytes.data(), frame.length);

        if constexpr (std::is_same_v<TargetEvent, SpiFrame<32>>) {
            m_sink.post(EventVariant{frame}, priority);
        } else {
            m_sink.post(EventVariant{TargetEvent{frame}}, priority);
        }
    }

private:
    EventSinkT<EventVariant> m_sink;
};

} // namespace corium::embedded

// <<< End: corium/embedded/SpiAdapter.hpp

// >>> Begin: corium/embedded/I2cAdapter.hpp
/**
 * @file I2cAdapter.hpp
 * @ingroup embedded
 * @brief Zero-heap I2C bus hardware ISR adapter for sensors and peripherals.
 */


#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>


namespace corium::embedded {

/// @brief I2C transaction direction/operation type.
enum class I2cOpType : uint8_t {
    Read = 0,
    Write = 1,
    WriteRead = 2
};

/// @brief Zero-heap I2C transaction result frame.
/// @tparam MaxPayload Maximum payload capacity in bytes (default: 32).
template <size_t MaxPayload = 32>
struct I2cFrame {
    uint16_t deviceAddress{0};     ///< 7-bit or 10-bit I2C target slave address
    uint8_t registerAddress{0};    ///< Internal register address (if applicable)
    I2cOpType opType{I2cOpType::Read}; ///< Read, Write, or WriteRead
    uint8_t status{0};             ///< Hardware status (0: ACK/Success, 1: NACK, 2: Bus Error)
    uint8_t length{0};             ///< Number of valid data bytes
    std::array<uint8_t, MaxPayload> data{}; ///< Payload buffer

    [[nodiscard]] std::span<const uint8_t> payload() const noexcept {
        return {data.data(), static_cast<size_t>(length > MaxPayload ? MaxPayload : length)};
    }
};

/// @ingroup embedded
/// @brief Non-blocking hardware ISR adapter for I2C master/slave controllers.
/// Dispatches completed I2C read/write transactions directly into the Corium lock-free ring buffer.
template <typename EventVariant>
class I2cIsrAdapter {
public:
    explicit I2cIsrAdapter(EventSinkT<EventVariant> sink) noexcept
        : m_sink(sink)
    {}

    /// @brief Post an I2C frame from ISR context into the event sink.
    /// @tparam TargetEvent User event type constructible from I2cFrame or I2cFrame itself.
    /// @tparam MaxPayload Payload capacity of the frame.
    /// @param frame The completed I2C transaction frame.
    /// @param priority Priority to assign.
    template <typename TargetEvent = I2cFrame<32>, size_t MaxPayload = 32>
    void postFromIsr(const I2cFrame<MaxPayload>& frame, EventPriority priority = EventPriority::Normal) noexcept {
        if constexpr (std::is_same_v<TargetEvent, I2cFrame<MaxPayload>>) {
            m_sink.post(EventVariant{frame}, priority);
        } else {
            m_sink.post(EventVariant{TargetEvent{frame}}, priority);
        }
    }

    /// @brief Construct and post an I2C read result frame from ISR context.
    /// @tparam TargetEvent User event type.
    /// @param devAddr Slave device address.
    /// @param regAddr Register address read from.
    /// @param readBytes Span of received bytes.
    /// @param status Hardware status code.
    /// @param priority Event priority.
    template <typename TargetEvent = I2cFrame<32>>
    void postReadFromIsr(
        uint16_t devAddr,
        uint8_t regAddr,
        std::span<const uint8_t> readBytes,
        uint8_t status = 0,
        EventPriority priority = EventPriority::Normal
    ) noexcept {
        I2cFrame<32> frame{};
        frame.deviceAddress = devAddr;
        frame.registerAddress = regAddr;
        frame.opType = I2cOpType::Read;
        frame.status = status;
        frame.length = static_cast<uint8_t>(readBytes.size() > 32 ? 32 : readBytes.size());
        std::memcpy(frame.data.data(), readBytes.data(), frame.length);

        if constexpr (std::is_same_v<TargetEvent, I2cFrame<32>>) {
            m_sink.post(EventVariant{frame}, priority);
        } else {
            m_sink.post(EventVariant{TargetEvent{frame}}, priority);
        }
    }

private:
    EventSinkT<EventVariant> m_sink;
};

} // namespace corium::embedded

// <<< End: corium/embedded/I2cAdapter.hpp

// <<< End: corium/embedded/embedded.hpp

// >>> Begin: corium/fsm/fsm.hpp
/**
 * @file fsm.hpp
 * @ingroup fsm
 * @brief Umbrella header for compile-time finite state machines.
 */



// >>> Begin: corium/fsm/Transition.hpp
/**
 * @file Transition.hpp
 * @ingroup fsm
 * @brief Declarative compile-time transition rules, internal transitions, and action lists.
 */


#include <tuple>
#include <utility>

namespace corium::fsm {

/// @brief Default always-true guard for state machine transitions.
struct Always {
    template <typename... Args>
    constexpr bool operator()(Args&&...) const noexcept { return true; }
};

/// @brief Default no-op action for state machine transitions.
struct NoAction {
    template <typename... Args>
    constexpr void operator()(Args&&...) const noexcept {}
};

/// @brief Compile-time transition rule definition.
/// @tparam From Source state type.
/// @tparam Event Trigger event type.
/// @tparam To Destination state type.
/// @tparam Guard Callable predicate (FromState&, const Event&) -> bool.
/// @tparam Action Callable action (FromState&, const Event&, ToState&) -> void.
template <
    typename From,
    typename Event,
    typename To,
    typename Guard = Always,
    typename Action = NoAction
>
struct Transition {
    using FromState = From;
    using EventType = Event;
    using ToState = To;
    using GuardType = Guard;
    using ActionType = Action;
    static constexpr bool is_internal = false;

    [[no_unique_address]] GuardType guard{};
    [[no_unique_address]] ActionType action{};

    constexpr Transition() = default;
    constexpr explicit Transition(GuardType g, ActionType a = ActionType{})
        : guard(std::move(g)), action(std::move(a))
    {}
};

/// @brief Compile-time internal transition rule (executes action without exiting or re-entering state).
template <
    typename State,
    typename Event,
    typename Guard = Always,
    typename Action = NoAction
>
struct InternalTransition {
    using FromState = State;
    using EventType = Event;
    using ToState = State;
    using GuardType = Guard;
    using ActionType = Action;
    static constexpr bool is_internal = true;

    [[no_unique_address]] GuardType guard{};
    [[no_unique_address]] ActionType action{};

    constexpr InternalTransition() = default;
    constexpr explicit InternalTransition(GuardType g, ActionType a = ActionType{})
        : guard(std::move(g)), action(std::move(a))
    {}
};

namespace detail {
template <typename Action, typename From, typename Event, typename To>
constexpr void execute_action(const Action& action, From& from, const Event& e, To& to);
}

/// @brief Sequential composition of multiple transition actions.
template <typename... Actions>
struct ActionList {
    std::tuple<Actions...> actions{};

    constexpr ActionList() = default;
    constexpr explicit ActionList(Actions... a) : actions(std::move(a)...) {}

    template <typename From, typename Event, typename To>
    constexpr void operator()(From& from, const Event& event, To& to) const {
        std::apply([&](const auto&... act) {
            (detail::execute_action(act, from, event, to), ...);
        }, actions);
    }
};

/// @brief Compile-time table containing all valid state transitions.
template <typename... Transitions>
struct TransitionTable {
    using TransitionsTuple = std::tuple<Transitions...>;
};

} // namespace corium::fsm

// <<< End: corium/fsm/Transition.hpp

// >>> Begin: corium/fsm/HistoryState.hpp
/**
 * @file HistoryState.hpp
 * @ingroup fsm
 * @brief Tag type for shallow history pseudostate in hierarchical state machines.
 */


namespace corium::fsm {

/// @ingroup fsm
/// @brief Tag type for designating a shallow history pseudostate in hierarchical state machines.
/// When entered, the state machine transitions to the most recently visited state in the group,
/// or to DefaultState if the group has not been visited yet.
/// @tparam DefaultState Fallback state if no history has been recorded.
template <typename DefaultState>
struct ShallowHistory {
    using DefaultStateType = DefaultState;
};

} // namespace corium::fsm

// <<< End: corium/fsm/HistoryState.hpp

// >>> Begin: corium/fsm/StateMachine.hpp
/**
 * @file StateMachine.hpp
 * @ingroup fsm
 * @brief Zero-heap variant-based active finite state machine coordinator.
 */


#include <type_traits>
#include <utility>
#include <variant>


namespace corium::fsm {

namespace detail {

template <typename State, typename Event>
constexpr void call_on_exit(State& s, const Event& e) {
    if constexpr (requires { s.onExit(e); }) {
        s.onExit(e);
    } else if constexpr (requires { s.onExit(); }) {
        s.onExit();
    }
}

template <typename State, typename Event>
constexpr void call_on_enter(State& s, const Event& e) {
    if constexpr (requires { s.onEnter(e); }) {
        s.onEnter(e);
    } else if constexpr (requires { s.onEnter(); }) {
        s.onEnter();
    }
}

template <typename Guard, typename State, typename Event>
constexpr bool evaluate_guard(const Guard& guard, const State& s, const Event& e) {
    if constexpr (std::is_invocable_r_v<bool, Guard, const State&, const Event&>) {
        return guard(s, e);
    } else if constexpr (std::is_invocable_r_v<bool, Guard, const Event&>) {
        return guard(e);
    } else if constexpr (std::is_invocable_r_v<bool, Guard>) {
        return guard();
    } else {
        return true;
    }
}

template <typename Action, typename From, typename Event, typename To>
constexpr void execute_action(const Action& action, From& from, const Event& e, To& to) {
    if constexpr (std::is_invocable_v<Action, From&, const Event&, To&>) {
        action(from, e, to);
    } else if constexpr (std::is_invocable_v<Action, From&, const Event&>) {
        action(from, e);
    } else if constexpr (std::is_invocable_v<Action, const Event&>) {
        action(e);
    } else if constexpr (std::is_invocable_v<Action>) {
        action();
    }
}

template <typename T>
struct is_internal_transition {
    static constexpr bool value = false;
};

template <typename T>
    requires requires { { T::is_internal } -> std::convertible_to<bool>; }
struct is_internal_transition<T> {
    static constexpr bool value = T::is_internal;
};

template <typename T>
inline constexpr bool is_internal_transition_v = is_internal_transition<T>::value;

} // namespace detail

/// @ingroup fsm
/// @brief Zero-heap, compile-time Finite State Machine.
/// @tparam Table TransitionTable defining valid state transitions.
/// @tparam InitialState Default initial active state.
/// @tparam OtherStates Additional valid states in the FSM state set.
template <
    typename Table,
    typename InitialState,
    typename... OtherStates
>
class StateMachine {
public:
    using StateVariant = std::variant<InitialState, OtherStates...>;
    using TransitionTableType = Table;

    constexpr StateMachine()
        : _state(InitialState{})
    {
        detail::call_on_enter(std::get<InitialState>(_state), int{0});
    }

    template <typename State>
        requires (std::is_constructible_v<StateVariant, State>)
    constexpr explicit StateMachine(State&& initial)
        : _state(std::forward<State>(initial))
    {
        std::visit([](auto& s) {
            detail::call_on_enter(s, int{0});
        }, _state);
    }

    /// @brief Check if current active state matches type State.
    template <typename State>
    [[nodiscard]] constexpr bool is() const noexcept {
        return std::holds_alternative<State>(_state);
    }

    /// @brief Access reference to current state as type State (throws std::bad_variant_access if mismatch).
    template <typename State>
    [[nodiscard]] constexpr State& as() {
        return std::get<State>(_state);
    }

    /// @brief Access const reference to current state as type State.
    template <typename State>
    [[nodiscard]] constexpr const State& as() const {
        return std::get<State>(_state);
    }

    /// @brief Access active state variant.
    [[nodiscard]] constexpr const StateVariant& state() const noexcept {
        return _state;
    }

    /// @brief Access active state variant.
    [[nodiscard]] constexpr StateVariant& state() noexcept {
        return _state;
    }

    /// @brief Process an incoming event through the state machine transition table.
    /// @tparam Event Trigger event type.
    /// @param event Event instance to evaluate.
    /// @return true if a transition was matched and executed; false if no transition applied.
    template <typename Event>
    bool process_event(const Event& event) {
        return std::visit([this, &event](auto& currentState) -> bool {
            using CurrentStateType = std::decay_t<decltype(currentState)>;
            return this->try_transition<CurrentStateType, Event>(currentState, event, Table{});
        }, _state);
    }

private:
    template <typename CurrentState, typename Event, typename... Transitions>
    bool try_transition(CurrentState& current, const Event& event, TransitionTable<Transitions...>) {
        return (try_single_transition<CurrentState, Event, Transitions>(current, event, Transitions{}) || ...);
    }

    template <typename CurrentState, typename Event, typename Trans>
    bool try_single_transition(CurrentState& current, const Event& event, const Trans& trans) {
        if constexpr (std::is_same_v<CurrentState, typename Trans::FromState> &&
                      std::is_same_v<std::decay_t<Event>, typename Trans::EventType>) {
            if (detail::evaluate_guard(trans.guard, current, event)) {
                if constexpr (detail::is_internal_transition_v<Trans>) {
                    detail::execute_action(trans.action, current, event, current);
                } else {
                    detail::call_on_exit(current, event);
                    typename Trans::ToState nextState{};
                    detail::execute_action(trans.action, current, event, nextState);
                    _state = std::move(nextState);
                    detail::call_on_enter(std::get<typename Trans::ToState>(_state), event);
                }
                return true;
            }
        }
        return false;
    }

    StateVariant _state;
};

} // namespace corium::fsm

// <<< End: corium/fsm/StateMachine.hpp

// <<< End: corium/fsm/fsm.hpp

// >>> Begin: corium/async/async.hpp
/**
 * @file async.hpp
 * @ingroup async
 * @brief Umbrella header for C++20 coroutine primitives.
 */



// >>> Begin: corium/async/FramePool.hpp
/**
 * @file FramePool.hpp
 * @ingroup async
 * @brief Zero-heap static coroutine frame pool and frame allocator policies.
 */


#include <array>
#include <cstddef>
#include <new>

namespace corium::async {

/// @brief Default heap allocator policy for C++20 coroutine frames (HALO-eligible).
struct HeapFrameAllocator {
    [[nodiscard]] static void* allocate(std::size_t size) {
        return ::operator new(size);
    }

    static void deallocate(void* ptr, [[maybe_unused]] std::size_t size) noexcept {
        ::operator delete(ptr);
    }
};

/// @ingroup async
/// @brief Statically-allocated fixed-capacity memory pool for coroutine state frames.
/// Eliminates heap allocations and guarantees deterministic O(1) coroutine frame allocation.
/// @tparam MaxFrames Maximum number of concurrent active coroutine frames.
/// @tparam FrameSize Maximum size in bytes reserved for each coroutine frame.
template <std::size_t MaxFrames = 16, std::size_t FrameSize = 256>
class StaticFramePool {
public:
    static constexpr std::size_t max_frames = MaxFrames;
    static constexpr std::size_t frame_size = FrameSize;

    struct alignas(std::max_align_t) Slot {
        std::byte storage[FrameSize];
        bool inUse{false};
    };

    [[nodiscard]] static void* allocate(std::size_t size) noexcept {
        if (size > FrameSize) {
            return nullptr;
        }

        for (auto& slot : _slots) {
            if (!slot.inUse) {
                slot.inUse = true;
                return static_cast<void*>(slot.storage);
            }
        }
        return nullptr; // Pool exhausted
    }

    static void deallocate(void* ptr, std::size_t /*size*/) noexcept {
        if (!ptr) {
            return;
        }

        for (auto& slot : _slots) {
            if (static_cast<void*>(slot.storage) == ptr) {
                slot.inUse = false;
                return;
            }
        }
    }

    /// @brief Number of active frames currently allocated from the pool.
    [[nodiscard]] static std::size_t activeCount() noexcept {
        std::size_t count = 0;
        for (const auto& slot : _slots) {
            if (slot.inUse) {
                count++;
            }
        }
        return count;
    }

    /// @brief Reset all pool slots to available state.
    static void reset() noexcept {
        for (auto& slot : _slots) {
            slot.inUse = false;
        }
    }

private:
    static inline std::array<Slot, MaxFrames> _slots{};
};

/// @brief Allocator policy adapter wrapping a StaticFramePool.
template <std::size_t MaxFrames = 16, std::size_t FrameSize = 256>
struct StaticFrameAllocator {
    using Pool = StaticFramePool<MaxFrames, FrameSize>;

    [[nodiscard]] static void* allocate(std::size_t size) {
        void* ptr = Pool::allocate(size);
        if (!ptr) {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
            throw std::bad_alloc();
#else
            return nullptr;
#endif
        }
        return ptr;
    }

    static void deallocate(void* ptr, std::size_t size) noexcept {
        Pool::deallocate(ptr, size);
    }
};

} // namespace corium::async

// <<< End: corium/async/FramePool.hpp

// >>> Begin: corium/async/Task.hpp
/**
 * @file Task.hpp
 * @ingroup async
 * @brief Lazy awaitable C++20 coroutine task with zero dynamic heap allocation.
 */


#include <coroutine>
#include <exception>
#include <utility>


namespace corium::async {

/// @ingroup async
/// @brief Lightweight C++20 coroutine task with zero-heap resumption chaining and configurable frame allocator.
/// @tparam T Result type returned by the coroutine (defaults to void).
/// @tparam Allocator Frame allocation policy (HeapFrameAllocator default or StaticFrameAllocator).
template <typename T = void, typename Allocator = HeapFrameAllocator>
class Task {
public:
    using ValueType = T;
    using AllocatorType = Allocator;

    struct promise_type {
        std::coroutine_handle<> continuation{nullptr};
        T value{};
        std::exception_ptr exception{nullptr};

        [[nodiscard]] static void* operator new(std::size_t size) {
            return Allocator::allocate(size);
        }

        static void operator delete(void* ptr, std::size_t size) noexcept {
            Allocator::deallocate(ptr, size);
        }

        Task get_return_object() noexcept {
            return Task(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        auto final_suspend() noexcept {
            struct FinalAwaiter {
                bool await_ready() noexcept { return false; }
                std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                    if (h.promise().continuation) {
                        return h.promise().continuation;
                    }
                    return std::noop_coroutine();
                }
                void await_resume() noexcept {}
            };
            return FinalAwaiter{};
        }

        template <typename Val>
            requires (std::is_convertible_v<Val, T>)
        void return_value(Val&& val) noexcept(std::is_nothrow_constructible_v<T, Val>) {
            value = std::forward<Val>(val);
        }

        void unhandled_exception() noexcept {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
            exception = std::current_exception();
#endif
        }
    };

    constexpr Task() noexcept = default;

    explicit Task(std::coroutine_handle<promise_type> handle) noexcept
        : _handle(handle)
    {}

    ~Task() {
        if (_handle) {
            _handle.destroy();
        }
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept
        : _handle(std::exchange(other._handle, nullptr))
    {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (_handle) {
                _handle.destroy();
            }
            _handle = std::exchange(other._handle, nullptr);
        }
        return *this;
    }

    /// @brief Check if coroutine has completed execution.
    [[nodiscard]] bool done() const noexcept {
        return !_handle || _handle.done();
    }

    /// @brief Resume the coroutine explicitly.
    void resume() {
        if (_handle && !_handle.done()) {
            _handle.resume();
        }
    }

    /// @brief Awaiter interface for co_await chaining.
    [[nodiscard]] bool await_ready() const noexcept {
        return !_handle || _handle.done();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        _handle.promise().continuation = awaiting;
        return _handle;
    }

    T await_resume() {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
        if (_handle.promise().exception) {
            std::rethrow_exception(_handle.promise().exception);
        }
#endif
        return std::move(_handle.promise().value);
    }

    /// @brief Access raw coroutine handle.
    [[nodiscard]] std::coroutine_handle<promise_type> handle() const noexcept {
        return _handle;
    }

private:
    std::coroutine_handle<promise_type> _handle{nullptr};
};

/// @brief Specialization of Task for void return type with configurable frame allocator.
template <typename Allocator>
class Task<void, Allocator> {
public:
    using ValueType = void;
    using AllocatorType = Allocator;

    struct promise_type {
        std::coroutine_handle<> continuation{nullptr};
        std::exception_ptr exception{nullptr};

        [[nodiscard]] static void* operator new(std::size_t size) {
            return Allocator::allocate(size);
        }

        static void operator delete(void* ptr, std::size_t size) noexcept {
            Allocator::deallocate(ptr, size);
        }

        Task get_return_object() noexcept {
            return Task(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        auto final_suspend() noexcept {
            struct FinalAwaiter {
                bool await_ready() noexcept { return false; }
                std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                    if (h.promise().continuation) {
                        return h.promise().continuation;
                    }
                    return std::noop_coroutine();
                }
                void await_resume() noexcept {}
            };
            return FinalAwaiter{};
        }

        void return_void() noexcept {}

        void unhandled_exception() noexcept {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
            exception = std::current_exception();
#endif
        }
    };

    constexpr Task() noexcept = default;

    explicit Task(std::coroutine_handle<promise_type> handle) noexcept
        : _handle(handle)
    {}

    ~Task() {
        if (_handle) {
            _handle.destroy();
        }
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept
        : _handle(std::exchange(other._handle, nullptr))
    {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (_handle) {
                _handle.destroy();
            }
            _handle = std::exchange(other._handle, nullptr);
        }
        return *this;
    }

    [[nodiscard]] bool done() const noexcept {
        return !_handle || _handle.done();
    }

    void resume() {
        if (_handle && !_handle.done()) {
            _handle.resume();
        }
    }

    [[nodiscard]] bool await_ready() const noexcept {
        return !_handle || _handle.done();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        _handle.promise().continuation = awaiting;
        return _handle;
    }

    void await_resume() {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
        if (_handle.promise().exception) {
            std::rethrow_exception(_handle.promise().exception);
        }
#endif
    }

    [[nodiscard]] std::coroutine_handle<promise_type> handle() const noexcept {
        return _handle;
    }

private:
    std::coroutine_handle<promise_type> _handle{nullptr};
};

/// @brief Pre-configured zero-heap statically-pooled coroutine task.
template <typename T = void, std::size_t MaxFrames = 16, std::size_t FrameSize = 256>
using PooledTask = Task<T, StaticFrameAllocator<MaxFrames, FrameSize>>;

} // namespace corium::async

// <<< End: corium/async/Task.hpp

// >>> Begin: corium/async/Delay.hpp
/**
 * @file Delay.hpp
 * @ingroup async
 * @brief Non-blocking timer delay and yield awaitables for C++20 coroutines.
 */


#include <chrono>
#include <coroutine>
#include <thread>

namespace corium::async {

/// @brief Awaitable that yields control back to the caller/event loop once.
struct YieldAwaiter {
    [[nodiscard]] constexpr bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) const noexcept {
        handle.resume();
    }
    constexpr void await_resume() const noexcept {}
};

/// @brief Helper to yield execution in a coroutine.
[[nodiscard]] inline constexpr YieldAwaiter yield() noexcept {
    return YieldAwaiter{};
}

/// @brief Awaitable that pauses the coroutine thread for the specified duration.
template <typename Rep, typename Period>
struct DelayAwaiter {
    std::chrono::duration<Rep, Period> duration;

    [[nodiscard]] constexpr bool await_ready() const noexcept {
        return duration.count() <= 0;
    }

    void await_suspend(std::coroutine_handle<> handle) const {
        std::this_thread::sleep_for(duration);
        handle.resume();
    }

    constexpr void await_resume() const noexcept {}
};

/// @brief Helper to suspend coroutine for a given std::chrono duration.
template <typename Rep, typename Period>
[[nodiscard]] inline auto delay(const std::chrono::duration<Rep, Period>& d) noexcept {
    return DelayAwaiter<Rep, Period>{d};
}

} // namespace corium::async

// <<< End: corium/async/Delay.hpp

// >>> Begin: corium/async/CancellationToken.hpp
/**
 * @file CancellationToken.hpp
 * @ingroup async
 * @brief Lock-free atomic cooperative cancellation token with coroutine awaiter.
 */


#include <atomic>
#include <coroutine>

namespace corium::async {

/// @ingroup async
/// @brief Lightweight, zero-heap cooperative cancellation token for C++20 coroutines and services.
class CancellationToken {
public:
    constexpr CancellationToken() noexcept = default;

    /// @brief Signal cancellation to all observing tasks.
    /// @note Wakes any suspended coroutine awaiting `whenCancelled()` immediately.
    void cancel() noexcept
    {
        _cancelled.store(true, std::memory_order_release);
        auto handle = _waiter.exchange(nullptr, std::memory_order_acq_rel);
        if (handle && !handle.done()) {
            handle.resume();
        }
    }

    /// @brief Check if cancellation has been requested.
    /// @return True if cancel() has been invoked, false otherwise.
    [[nodiscard]] bool isCancelled() const noexcept
    {
        return _cancelled.load(std::memory_order_acquire);
    }

    /// @brief Reset token state to uncancelled.
    /// @note Allows reusing the token for subsequent asynchronous task executions.
    void reset() noexcept
    {
        _cancelled.store(false, std::memory_order_release);
        _waiter.store(nullptr, std::memory_order_release);
    }

    /// @brief Awaitable that suspends until this token is cancelled.
    struct WhenCancelledAwaiter {
        CancellationToken& token;

        [[nodiscard]] bool await_ready() const noexcept
        {
            return token.isCancelled();
        }

        bool await_suspend(std::coroutine_handle<> h) noexcept
        {
            if (token.isCancelled()) {
                return false; // do not suspend
            }
            token._waiter.store(h, std::memory_order_release);
            return !token.isCancelled();
        }

        constexpr void await_resume() const noexcept {}
    };

    /// @brief Helper to suspend the current coroutine until cancel() is called.
    /// @return WhenCancelledAwaiter that suspends until token cancellation.
    /// @example
    /// Task<void> worker(CancellationToken token) {
    ///     co_await token.whenCancelled();
    ///     // Clean up resources on shutdown signal
    /// }
    [[nodiscard]] WhenCancelledAwaiter whenCancelled() noexcept
    {
        return WhenCancelledAwaiter{*this};
    }

private:
    std::atomic<bool> _cancelled{false};
    std::atomic<std::coroutine_handle<>> _waiter{nullptr};
};

} // namespace corium::async

// <<< End: corium/async/CancellationToken.hpp

// >>> Begin: corium/async/WhenAll.hpp
/**
 * @file WhenAll.hpp
 * @ingroup async
 * @brief Non-blocking combinator awaiting completion of multiple parallel tasks.
 */


#include <tuple>
#include <type_traits>
#include <utility>


namespace corium::async {

namespace detail {

template <typename... Tasks>
inline constexpr bool all_void_tasks_v = (std::is_void_v<typename std::decay_t<Tasks>::ValueType> && ...);

template <typename... Tasks>
Task<std::tuple<typename std::decay_t<Tasks>::ValueType...>> whenAllValueImpl(Tasks... tasks)
{
    co_return std::tuple<typename std::decay_t<Tasks>::ValueType...>{(co_await tasks)...};
}

template <typename... Tasks>
Task<void> whenAllVoidImpl(Tasks... tasks)
{
    ((void)(co_await tasks), ...);
    co_return;
}

} // namespace detail

/// @ingroup async
/// @brief Awaits concurrent or sequential completion of multiple Task coroutines.
/// @return Task containing std::tuple of results, or Task<void> if all input tasks are void.
template <typename... Tasks>
auto whenAll(Tasks&&... tasks)
{
    if constexpr (detail::all_void_tasks_v<Tasks...>) {
        return detail::whenAllVoidImpl(std::forward<Tasks>(tasks)...);
    } else {
        return detail::whenAllValueImpl(std::forward<Tasks>(tasks)...);
    }
}

} // namespace corium::async

// <<< End: corium/async/WhenAll.hpp

// >>> Begin: corium/async/WhenAny.hpp
/**
 * @file WhenAny.hpp
 * @ingroup async
 * @brief Non-blocking combinator resolving on the first completed task.
 */


#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>


namespace corium::async {

namespace detail {

template <typename T>
struct WrapVoid {
    using type = T;
};

template <>
struct WrapVoid<void> {
    using type = std::monostate;
};

template <typename T>
using wrap_void_t = typename WrapVoid<T>::type;

} // namespace detail

/// @brief Result container for whenAny combinator.
template <typename... ResultTypes>
struct WhenAnyResult {
    std::size_t index{0};
    std::variant<detail::wrap_void_t<ResultTypes>...> result{};
};

namespace detail {

template <size_t Index, typename TaskType, typename ResultVariant>
bool checkTaskDone(TaskType& task, size_t& winnerIndex, ResultVariant& resultVariant)
{
    if (task.done()) {
        winnerIndex = Index;
        if constexpr (!std::is_void_v<typename TaskType::ValueType>) {
            resultVariant.template emplace<Index>(task.await_resume());
        } else {
            task.await_resume();
            resultVariant.template emplace<Index>(std::monostate{});
        }
        return true;
    }
    return false;
}

template <size_t Index, typename TaskType>
void resumeTask(TaskType& task)
{
    if (!task.done()) {
        task.resume();
    }
}

template <typename... Tasks, size_t... Is>
Task<WhenAnyResult<typename std::decay_t<Tasks>::ValueType...>> whenAnyImpl(std::index_sequence<Is...>, Tasks... tasks)
{
    using ResultType = WhenAnyResult<typename std::decay_t<Tasks>::ValueType...>;
    ResultType res{};

    // Initial resume pass
    (resumeTask<Is>(tasks), ...);

    while (true) {
        bool winnerFound = (checkTaskDone<Is>(tasks, res.index, res.result) || ...);
        if (winnerFound) {
            break;
        }
        (resumeTask<Is>(tasks), ...);
    }

    co_return res;
}

} // namespace detail

/// @ingroup async
/// @brief Awaits the first task among multiple tasks to complete.
/// @return Task containing WhenAnyResult with index and variant result.
template <typename... Tasks>
auto whenAny(Tasks&&... tasks)
{
    return detail::whenAnyImpl(
        std::index_sequence_for<Tasks...>{},
        std::forward<Tasks>(tasks)...
    );
}

} // namespace corium::async

// <<< End: corium/async/WhenAny.hpp

// >>> Begin: corium/async/Generator.hpp
/**
 * @file Generator.hpp
 * @ingroup async
 * @brief Pull-based zero-heap lazy sequence generator compatible with C++20 ranges.
 */


#include <coroutine>
#include <exception>
#include <iterator>
#include <utility>


namespace corium::async {

/// @ingroup async
/// @brief Zero-heap C++20 pull-based lazy generator sequence with configurable frame allocator.
/// Compatible with range-based for loops and standard C++20 ranges.
/// @tparam T Value type yielded by the generator.
/// @tparam Allocator Frame allocation policy (HeapFrameAllocator default or StaticFrameAllocator).
template <typename T, typename Allocator = HeapFrameAllocator>
class Generator {
public:
    using ValueType = T;
    using AllocatorType = Allocator;

    struct promise_type {
        const T* currentValue{nullptr};
        std::exception_ptr exception{nullptr};

        [[nodiscard]] static void* operator new(std::size_t size) {
            return Allocator::allocate(size);
        }

        static void operator delete(void* ptr, std::size_t size) noexcept {
            Allocator::deallocate(ptr, size);
        }

        Generator get_return_object() noexcept
        {
            return Generator(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        std::suspend_always yield_value(const T& val) noexcept
        {
            currentValue = std::addressof(val);
            return {};
        }

        std::suspend_always yield_value(T&& val) noexcept
        {
            currentValue = std::addressof(val);
            return {};
        }

        void return_void() noexcept {}

        void unhandled_exception() noexcept
        {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
            exception = std::current_exception();
#endif
        }
    };

    class Sentinel {};

    class Iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using reference = const T&;
        using pointer = const T*;

        constexpr Iterator() noexcept = default;

        explicit Iterator(std::coroutine_handle<promise_type> handle) noexcept
            : _handle(handle)
        {}

        Iterator& operator++()
        {
            _handle.resume();
            if (_handle.done()) {
                if (_handle.promise().exception) {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
                    std::rethrow_exception(_handle.promise().exception);
#endif
                }
            }
            return *this;
        }

        void operator++(int)
        {
            (void)operator++();
        }

        [[nodiscard]] reference operator*() const noexcept
        {
            return *_handle.promise().currentValue;
        }

        [[nodiscard]] pointer operator->() const noexcept
        {
            return _handle.promise().currentValue;
        }

        [[nodiscard]] bool operator==(Sentinel) const noexcept
        {
            return !_handle || _handle.done();
        }

        [[nodiscard]] bool operator!=(Sentinel s) const noexcept
        {
            return !(*this == s);
        }

    private:
        std::coroutine_handle<promise_type> _handle{nullptr};
    };

    constexpr Generator() noexcept = default;

    explicit Generator(std::coroutine_handle<promise_type> handle) noexcept
        : _handle(handle)
    {}

    ~Generator()
    {
        if (_handle) {
            _handle.destroy();
        }
    }

    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    Generator(Generator&& other) noexcept
        : _handle(std::exchange(other._handle, nullptr))
    {}

    Generator& operator=(Generator&& other) noexcept
    {
        if (this != &other) {
            if (_handle) {
                _handle.destroy();
            }
            _handle = std::exchange(other._handle, nullptr);
        }
        return *this;
    }

    [[nodiscard]] Iterator begin()
    {
        if (_handle) {
            _handle.resume();
            if (_handle.promise().exception) {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
                std::rethrow_exception(_handle.promise().exception);
#endif
            }
        }
        return Iterator{_handle};
    }

    [[nodiscard]] constexpr Sentinel end() noexcept
    {
        return Sentinel{};
    }

private:
    std::coroutine_handle<promise_type> _handle{nullptr};
};

/// @brief Pre-configured zero-heap statically-pooled lazy generator sequence.
template <typename T, std::size_t MaxFrames = 16, std::size_t FrameSize = 256>
using PooledGenerator = Generator<T, StaticFrameAllocator<MaxFrames, FrameSize>>;

} // namespace corium::async

// <<< End: corium/async/Generator.hpp

// >>> Begin: corium/async/AsyncEvent.hpp
/**
 * @file AsyncEvent.hpp
 * @ingroup async
 * @brief Lock-free asynchronous event synchronization primitive for C++20 coroutines.
 */


#include <atomic>
#include <coroutine>
#include <type_traits>
#include <utility>

namespace corium::async {

/// @ingroup async
/// @brief Lock-free single-waiter asynchronous event channel for C++20 coroutines.
/// Enables event-driven coroutine resumption without blocking threads or allocating dynamic memory.
/// @tparam T Payload type transferred when event is signaled (defaults to void).
template <typename T = void>
class AsyncEvent {
public:
    using ValueType = T;

    constexpr AsyncEvent() = default;

    /// @brief Emit event value and resume waiting coroutine.
    template <typename U>
        requires (std::is_convertible_v<U, T>)
    void emit(U&& value) {
        _value = std::forward<U>(value);
        _ready.store(true, std::memory_order_release);
        auto h = _waiter.exchange(nullptr, std::memory_order_acq_rel);
        if (h && !h.done()) {
            h.resume();
        }
    }

    /// @brief Reset event to unsignaled state.
    void reset() noexcept {
        _ready.store(false, std::memory_order_release);
        _waiter.store(nullptr, std::memory_order_release);
    }

    /// @brief Check if event has been signaled.
    [[nodiscard]] bool isReady() const noexcept {
        return _ready.load(std::memory_order_acquire);
    }

    struct Awaiter {
        AsyncEvent& event;

        [[nodiscard]] bool await_ready() const noexcept {
            return event.isReady();
        }

        bool await_suspend(std::coroutine_handle<> h) noexcept {
            if (event.isReady()) {
                return false;
            }
            event._waiter.store(h, std::memory_order_release);
            return !event.isReady();
        }

        T await_resume() {
            event._ready.store(false, std::memory_order_release);
            return std::move(event._value);
        }
    };

    /// @brief Awaiter factory for co_await syntax.
    [[nodiscard]] Awaiter wait() noexcept {
        return Awaiter{*this};
    }

    /// @brief Direct co_await support on AsyncEvent instance.
    [[nodiscard]] Awaiter operator co_await() noexcept {
        return Awaiter{*this};
    }

private:
    std::atomic<std::coroutine_handle<>> _waiter{nullptr};
    T _value{};
    std::atomic<bool> _ready{false};
};

/// @brief Specialization of AsyncEvent for void signaling.
template <>
class AsyncEvent<void> {
public:
    using ValueType = void;

    constexpr AsyncEvent() = default;

    /// @brief Signal the event and resume waiting coroutine.
    void emit() noexcept {
        _ready.store(true, std::memory_order_release);
        auto h = _waiter.exchange(nullptr, std::memory_order_acq_rel);
        if (h && !h.done()) {
            h.resume();
        }
    }

    /// @brief Reset event to unsignaled state.
    void reset() noexcept {
        _ready.store(false, std::memory_order_release);
        _waiter.store(nullptr, std::memory_order_release);
    }

    /// @brief Check if event has been signaled.
    [[nodiscard]] bool isReady() const noexcept {
        return _ready.load(std::memory_order_acquire);
    }

    struct Awaiter {
        AsyncEvent<void>& event;

        [[nodiscard]] bool await_ready() const noexcept {
            return event.isReady();
        }

        bool await_suspend(std::coroutine_handle<> h) noexcept {
            if (event.isReady()) {
                return false;
            }
            event._waiter.store(h, std::memory_order_release);
            return !event.isReady();
        }

        constexpr void await_resume() const noexcept {}
    };

    /// @brief Awaiter factory for co_await syntax.
    [[nodiscard]] Awaiter wait() noexcept {
        return Awaiter{*this};
    }

    /// @brief Direct co_await support on AsyncEvent instance.
    [[nodiscard]] Awaiter operator co_await() noexcept {
        return Awaiter{*this};
    }

private:
    std::atomic<std::coroutine_handle<>> _waiter{nullptr};
    std::atomic<bool> _ready{false};
};

} // namespace corium::async

// <<< End: corium/async/AsyncEvent.hpp

// >>> Begin: corium/async/Channel.hpp
/**
 * @file Channel.hpp
 * @ingroup async
 * @brief Zero-heap bounded asynchronous channel for C++20 coroutine message passing.
 */


#include <array>
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <optional>
#include <utility>

namespace corium::async {

/// @ingroup async
/// @brief Statically allocated bounded asynchronous channel for typed producer-consumer coroutines.
/// @tparam T Element type transferred through the channel.
/// @tparam Capacity Maximum capacity of the channel ring buffer (must be power of two or positive).
template <typename T, size_t Capacity = 16>
class Channel {
    static_assert(Capacity > 0, "Channel capacity must be greater than zero.");

public:
    using ValueType = T;

    constexpr Channel() noexcept = default;

    ~Channel() {
        close();
    }

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    /// @brief Non-blocking attempt to push an item into the channel.
    /// @param value Item to push.
    /// @return true if pushed; false if channel is full or closed.
    bool tryPush(T value) noexcept {
        if (m_closed.load(std::memory_order_acquire)) {
            return false;
        }

        size_t count = m_count.load(std::memory_order_relaxed);
        if (count >= Capacity) {
            return false;
        }

        size_t tail = m_tail.load(std::memory_order_relaxed);
        m_buffer[tail] = std::move(value);
        m_tail.store((tail + 1) % Capacity, std::memory_order_relaxed);
        m_count.fetch_add(1, std::memory_order_release);

        // Resume any waiting consumer
        auto h = m_recvWaiter.exchange(nullptr, std::memory_order_acq_rel);
        if (h && !h.done()) {
            h.resume();
        }
        return true;
    }

    /// @brief Non-blocking attempt to pop an item from the channel.
    /// @param out Variable to receive the popped item.
    /// @return true if popped; false if channel is empty.
    bool tryPop(T& out) noexcept {
        size_t count = m_count.load(std::memory_order_relaxed);
        if (count == 0) {
            return false;
        }

        size_t head = m_head.load(std::memory_order_relaxed);
        out = std::move(m_buffer[head]);
        m_head.store((head + 1) % Capacity, std::memory_order_relaxed);
        m_count.fetch_sub(1, std::memory_order_release);

        // Resume any waiting producer
        auto h = m_sendWaiter.exchange(nullptr, std::memory_order_acq_rel);
        if (h && !h.done()) {
            h.resume();
        }
        return true;
    }

    /// @brief Close the channel. No more pushes will succeed. Remaining elements can still be popped.
    void close() noexcept {
        m_closed.store(true, std::memory_order_release);
        auto hr = m_recvWaiter.exchange(nullptr, std::memory_order_acq_rel);
        if (hr && !hr.done()) {
            hr.resume();
        }
        auto hs = m_sendWaiter.exchange(nullptr, std::memory_order_acq_rel);
        if (hs && !hs.done()) {
            hs.resume();
        }
    }

    /// @brief Check if channel is closed.
    [[nodiscard]] bool isClosed() const noexcept {
        return m_closed.load(std::memory_order_acquire);
    }

    /// @brief Number of elements currently in the channel.
    [[nodiscard]] size_t size() const noexcept {
        return m_count.load(std::memory_order_relaxed);
    }

    /// @brief Check if channel is empty.
    [[nodiscard]] bool empty() const noexcept {
        return size() == 0;
    }

    /// @brief Check if channel is full.
    [[nodiscard]] bool full() const noexcept {
        return size() >= Capacity;
    }

    /// @brief Awaiter for pushing an item asynchronously (suspends if channel is full).
    struct PushAwaiter {
        Channel& chan;
        T val;
        bool done{false};

        [[nodiscard]] bool await_ready() noexcept {
            if (chan.isClosed()) {
                done = true;
                return true;
            }
            if (chan.tryPush(std::move(val))) {
                done = true;
                return true;
            }
            return false;
        }

        bool await_suspend(std::coroutine_handle<> h) noexcept {
            chan.m_pendingPushVal = std::move(val);
            chan.m_sendWaiter.store(h, std::memory_order_release);
            return true;
        }

        bool await_resume() noexcept {
            if (done) return !chan.isClosed();
            if (chan.m_pendingPushVal.has_value()) {
                bool pushed = chan.tryPush(std::move(*chan.m_pendingPushVal));
                chan.m_pendingPushVal.reset();
                return pushed;
            }
            return false;
        }
    };

    /// @brief Push an item into the channel asynchronously with backpressure suspension.
    /// @param val Item to push.
    /// @return Awaiter resolving to true if pushed, false if channel closed.
    [[nodiscard]] PushAwaiter push(T val) noexcept {
        return PushAwaiter{*this, std::move(val), false};
    }

    /// @brief Awaiter for popping an item asynchronously (suspends if channel is empty).
    struct PopAwaiter {
        Channel& chan;
        std::optional<T> result{};

        [[nodiscard]] bool await_ready() noexcept {
            T item{};
            if (chan.tryPop(item)) {
                result = std::move(item);
                return true;
            }
            if (chan.isClosed()) {
                result = std::nullopt;
                return true;
            }
            return false;
        }

        bool await_suspend(std::coroutine_handle<> h) noexcept {
            T item{};
            if (chan.tryPop(item)) {
                result = std::move(item);
                return false;
            }
            if (chan.isClosed()) {
                result = std::nullopt;
                return false;
            }
            chan.m_recvWaiter.store(h, std::memory_order_release);
            return true;
        }

        std::optional<T> await_resume() noexcept {
            if (result.has_value()) {
                return result;
            }
            T item{};
            if (chan.tryPop(item)) {
                return item;
            }
            return std::nullopt;
        }
    };

    /// @brief Pop an item from the channel asynchronously.
    /// @return Awaiter resolving to std::optional<T> (std::nullopt when closed and drained).
    [[nodiscard]] PopAwaiter pop() noexcept {
        return PopAwaiter{*this};
    }

private:
    std::array<T, Capacity> m_buffer{};
    std::atomic<size_t> m_head{0};
    std::atomic<size_t> m_tail{0};
    std::atomic<size_t> m_count{0};
    std::atomic<bool> m_closed{false};

    std::atomic<std::coroutine_handle<>> m_recvWaiter{nullptr};
    std::atomic<std::coroutine_handle<>> m_sendWaiter{nullptr};
    std::optional<T> m_pendingPushVal{};
};

} // namespace corium::async

// <<< End: corium/async/Channel.hpp

// >>> Begin: corium/async/Semaphore.hpp
/**
 * @file Semaphore.hpp
 * @ingroup async
 * @brief Zero-heap asynchronous counting semaphore for C++20 coroutines.
 */


#include <atomic>
#include <coroutine>
#include <cstddef>

namespace corium::async {

/// @ingroup async
/// @brief Asynchronous counting semaphore for cooperative coroutine concurrency throttling.
class AsyncSemaphore {
public:
    /// @brief Construct semaphore with initial available count.
    /// @param initialCount Number of initial available permits.
    explicit constexpr AsyncSemaphore(ptrdiff_t initialCount = 1) noexcept
        : m_count(initialCount)
    {}

    ~AsyncSemaphore() = default;
    AsyncSemaphore(const AsyncSemaphore&) = delete;
    AsyncSemaphore& operator=(const AsyncSemaphore&) = delete;

    /// @brief Non-blocking attempt to acquire a permit.
    /// @return true if permit acquired; false if semaphore count is zero.
    bool tryAcquire() noexcept {
        ptrdiff_t current = m_count.load(std::memory_order_relaxed);
        while (current > 0) {
            if (m_count.compare_exchange_weak(current, current - 1,
                                               std::memory_order_acquire,
                                               std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }

    /// @brief Release one or more permits and resume waiting coroutines.
    /// @param update Number of permits to return (default: 1).
    void release(ptrdiff_t update = 1) noexcept {
        m_count.fetch_add(update, std::memory_order_release);
        auto h = m_waiter.exchange(nullptr, std::memory_order_acq_rel);
        if (h && !h.done()) {
            h.resume();
        }
    }

    /// @brief Available permit count.
    [[nodiscard]] ptrdiff_t available() const noexcept {
        return m_count.load(std::memory_order_relaxed);
    }

    /// @brief Awaiter for acquiring a permit asynchronously.
    struct AcquireAwaiter {
        AsyncSemaphore& sem;

        [[nodiscard]] bool await_ready() const noexcept {
            return sem.tryAcquire();
        }

        bool await_suspend(std::coroutine_handle<> h) noexcept {
            if (sem.tryAcquire()) {
                return false;
            }
            sem.m_waiter.store(h, std::memory_order_release);
            return !sem.tryAcquire();
        }

        constexpr void await_resume() const noexcept {}
    };

    /// @brief Acquire a permit asynchronously (suspends coroutine until permit is released).
    [[nodiscard]] AcquireAwaiter acquire() noexcept {
        return AcquireAwaiter{*this};
    }

private:
    std::atomic<ptrdiff_t> m_count{1};
    std::atomic<std::coroutine_handle<>> m_waiter{nullptr};
};

} // namespace corium::async

// <<< End: corium/async/Semaphore.hpp

// <<< End: corium/async/async.hpp

// >>> Begin: corium/wire/wire.hpp
/**
 * @file wire.hpp
 * @ingroup wire
 * @brief Umbrella header for binary wire protocol framing and serialization.
 */



// >>> Begin: corium/wire/WirePacket.hpp
/**
 * @file WirePacket.hpp
 * @ingroup wire
 * @brief Binary packet framing with CRC-16 checksum and schema versioning.
 */


#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace corium::wire {

/// @brief Default magic header identifier for Corium binary wire packets (0xC041).
inline constexpr uint16_t CORIUM_WIRE_MAGIC = 0xC041;

/// @brief Calculate CRC-16-CCITT checksum over a byte span without lookup tables.
[[nodiscard]] constexpr uint16_t calculateCrc16(std::span<const uint8_t> data) noexcept {
    uint16_t crc = 0xFFFF;
    for (uint8_t byte : data) {
        crc ^= static_cast<uint16_t>(byte) << 8;
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

/// @brief Calculate an ABI type signature based on size and alignment.
template <typename T>
[[nodiscard]] constexpr uint8_t computeTypeSignature() noexcept {
    auto sig = static_cast<uint8_t>(sizeof(T) & 0x0F);
    sig |= static_cast<uint8_t>((alignof(T) & 0x0F) << 4);
    return sig;
}

/// @brief Current schema version for Corium binary wire packets.
inline constexpr uint8_t CORIUM_WIRE_VERSION = 1;

/// @brief Header structure framing binary wire packets for serial, CAN, SPI, or network transport.
#pragma pack(push, 1)
struct WireHeader {
    uint16_t magic{CORIUM_WIRE_MAGIC};
    uint8_t version{CORIUM_WIRE_VERSION};
    uint8_t typeIndex{0};
    uint8_t flags{0};
    uint8_t reserved{0};
    uint16_t payloadLength{0};
    uint16_t checksum{0};
};
#pragma pack(pop)

/// @ingroup wire
/// @brief Statically-sized zero-heap binary wire packet.
/// @tparam MaxPayloadSize Maximum payload capacity in bytes (default: 64).
template <size_t MaxPayloadSize = 64>
struct WirePacket {
    WireHeader header{};
    std::array<uint8_t, MaxPayloadSize> payload{};

    constexpr WirePacket() = default;

    /// @brief Finalize packet header, set payload length and calculate CRC16 checksum.
    void finalize(uint8_t typeIdx, uint16_t length, uint8_t flags = 0, uint8_t version = CORIUM_WIRE_VERSION) noexcept {
        header.magic = CORIUM_WIRE_MAGIC;
        header.version = version;
        header.typeIndex = typeIdx;
        header.flags = flags;
        header.reserved = 0;
        header.payloadLength = length > MaxPayloadSize ? static_cast<uint16_t>(MaxPayloadSize) : length;
        header.checksum = calculateCrc16(std::span<const uint8_t>(payload.data(), header.payloadLength));
    }

    /// @brief Validate packet magic identifier, schema version, payload length bounds, and CRC16 checksum.
    [[nodiscard]] bool isValid() const noexcept {
        if (header.magic != CORIUM_WIRE_MAGIC) {
            return false;
        }
        if (header.version != CORIUM_WIRE_VERSION) {
            return false;
        }
        if (header.payloadLength > MaxPayloadSize) {
            return false;
        }
        uint16_t expected = calculateCrc16(std::span<const uint8_t>(payload.data(), header.payloadLength));
        return header.checksum == expected;
    }

    /// @brief Total serialized wire size in bytes (header + payload length).
    [[nodiscard]] constexpr size_t totalWireSize() const noexcept {
        return sizeof(WireHeader) + header.payloadLength;
    }
};

} // namespace corium::wire

// <<< End: corium/wire/WirePacket.hpp

// >>> Begin: corium/wire/Serializer.hpp
/**
 * @file Serializer.hpp
 * @ingroup wire
 * @brief Type-safe serialization and direct event sink deserialization.
 */


#include <cstring>
#include <type_traits>
#include <utility>
#include <variant>


namespace corium::wire {

/// @ingroup wire
/// @brief Zero-heap type-safe serializer and deserializer for Corium event variants over binary wire streams.
class WireSerializer {
public:
    /// @brief Serialize a trivially-copyable concrete event into a fixed-size binary WirePacket.
    /// @tparam Event Concrete event type to serialize.
    /// @tparam EventVariant Target variant list to determine type index.
    /// @tparam MaxPayload Payload size limit.
    /// @param event Event instance to serialize.
    /// @return Serialized and CRC-validated WirePacket.
    template <
        typename Event,
        typename EventVariant,
        size_t MaxPayload = 64
    >
    [[nodiscard]] static WirePacket<MaxPayload> serialize(const Event& event) noexcept {
        static_assert(std::is_trivially_copyable_v<Event>, "Wire events must be trivially copyable for zero-copy binary transport.");
        static_assert(sizeof(Event) <= MaxPayload, "Event size exceeds WirePacket MaxPayload capacity.");

        constexpr size_t typeIdx = corium::variant_index_v<Event, EventVariant>;
        static_assert(typeIdx != static_cast<size_t>(-1), "Event type is not part of the specified EventVariant list.");

        WirePacket<MaxPayload> packet;
        std::memcpy(packet.payload.data(), &event, sizeof(Event));
        packet.finalize(static_cast<uint8_t>(typeIdx), static_cast<uint16_t>(sizeof(Event)));
        packet.header.reserved = computeTypeSignature<Event>();
        return packet;
    }

    /// @brief Deserialize a validated binary WirePacket directly into an event sink.
    /// @tparam EventVariant Variant list of all supported events.
    /// @tparam MaxPayload Payload size limit.
    /// @tparam Sink Target EventSink or EventBus.
    /// @param packet Incoming packet to validate and deserialize.
    /// @param sink Event sink to receive the deserialized event.
    /// @param priority Priority to assign to the deserialized event.
    /// @return true if packet was valid and successfully dispatched into sink; false on validation/type error.
    template <
        typename EventVariant,
        size_t MaxPayload = 64,
        typename Sink
    >
    static bool deserializeAndPush(
        const WirePacket<MaxPayload>& packet,
        Sink& sink,
        EventPriority priority = EventPriority::Normal
    ) noexcept {
        if (!packet.isValid()) {
            return false;
        }

        constexpr size_t numTypes = std::variant_size_v<EventVariant>;
        if (packet.header.typeIndex >= numTypes) {
            return false;
        }

        return deserializeIndex<EventVariant, MaxPayload, Sink>(
            packet,
            sink,
            priority,
            std::make_index_sequence<numTypes>{}
        );
    }

private:
    template <typename EventVariant, size_t MaxPayload, typename Sink, size_t... Is>
    static bool deserializeIndex(
        const WirePacket<MaxPayload>& packet,
        Sink& sink,
        EventPriority priority,
        std::index_sequence<Is...>
    ) noexcept {
        bool handled = false;
        (void)((packet.header.typeIndex == Is ? (handled = deserializeExact<Is, EventVariant, MaxPayload, Sink>(packet, sink, priority), true) : false) || ...);
        return handled;
    }

    template <size_t Index, typename EventVariant, size_t MaxPayload, typename Sink>
    static bool deserializeExact(
        const WirePacket<MaxPayload>& packet,
        Sink& sink,
        EventPriority priority
    ) noexcept {
        using TargetEvent = std::variant_alternative_t<Index, EventVariant>;
        if (packet.header.payloadLength != sizeof(TargetEvent)) {
            return false;
        }

        if (packet.header.reserved != 0 && packet.header.reserved != computeTypeSignature<TargetEvent>()) {
            return false; // ABI size/alignment signature mismatch
        }

        TargetEvent evt{};
        std::memcpy(&evt, packet.payload.data(), sizeof(TargetEvent));
        sink.post(EventVariant{std::move(evt)}, priority);
        return true;
    }
};

} // namespace corium::wire

// <<< End: corium/wire/Serializer.hpp

// >>> Begin: corium/wire/EventJournal.hpp
/**
 * @file EventJournal.hpp
 * @ingroup wire
 * @brief Zero-heap binary event journal for deterministic recording and replay.
 */


#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>


namespace corium::wire {

/// @brief Magic identifier for Corium binary event journals ("JOUR" in hex: 0x4a4f5552).
inline constexpr uint32_t CORIUM_JOURNAL_MAGIC = 0x4a4f5552;

/// @brief Current schema version for Corium event journals.
inline constexpr uint32_t CORIUM_JOURNAL_VERSION = 1;

/// @brief Computes a deterministic 64-bit ABI hash for an EventVariant type.
template <typename EventVariant>
[[nodiscard]] constexpr uint64_t computeVariantSchemaHash() noexcept {
    constexpr size_t numTypes = std::variant_size_v<EventVariant>;
    uint64_t hash = 0xcbf29ce484222325ULL; // FNV-1a 64-bit basis
    hash ^= static_cast<uint64_t>(numTypes);
    hash *= 0x100000001b3ULL;
    return hash;
}

#pragma pack(push, 1)

/// @brief Header placed at the beginning of an event journal binary stream.
struct JournalHeader {
    uint32_t magic{CORIUM_JOURNAL_MAGIC};
    uint32_t version{CORIUM_JOURNAL_VERSION};
    uint64_t schemaHash{0};
    uint32_t recordCount{0};
    uint32_t reserved{0};
};

/// @brief Header preceding every serialized event record in the journal.
struct JournalRecordHeader {
    uint64_t timestampUs{0};
    uint32_t typeIndex{0};
    uint32_t payloadLength{0};
    uint16_t checksum{0};
    uint8_t priority{static_cast<uint8_t>(EventPriority::Normal)};
    uint8_t typeSignature{0};
};

#pragma pack(pop)

/// @ingroup wire
/// @brief Statically allocated binary event journal writer for zero-heap post-mortem logging and record playback.
/// @tparam EventVariant Variant of supported event types.
/// @tparam BufferCapacity Total byte capacity of the journal storage buffer.
template <typename EventVariant, size_t BufferCapacity = 4096>
class EventJournalWriter {
public:
    constexpr EventJournalWriter() noexcept {
        initHeader();
    }

    /// @brief Reset journal to initial empty state.
    void reset() noexcept {
        m_offset = 0;
        m_recordCount = 0;
        initHeader();
    }

    /// @brief Record a concrete typed event into the journal.
    /// @tparam Event Concrete event struct type (must be trivially copyable).
    /// @param event Event instance to record.
    /// @param timestampUs Timestamp in microseconds.
    /// @param priority Priority of the event.
    /// @return true if record was successfully written; false if journal is full.
    template <typename Event>
    bool record(const Event& event, uint64_t timestampUs, EventPriority priority = EventPriority::Normal) noexcept {
        static_assert(std::is_trivially_copyable_v<Event>, "Event must be trivially copyable for journal serialization.");
        constexpr size_t typeIdx = corium::variant_index_v<Event, EventVariant>;
        static_assert(typeIdx != static_cast<size_t>(-1), "Event type is not in the specified EventVariant.");

        constexpr size_t recordSize = sizeof(JournalRecordHeader) + sizeof(Event);
        if (m_offset + recordSize > BufferCapacity) {
            return false;
        }

        JournalRecordHeader recHeader{};
        recHeader.timestampUs = timestampUs;
        recHeader.typeIndex = static_cast<uint32_t>(typeIdx);
        recHeader.payloadLength = static_cast<uint32_t>(sizeof(Event));
        recHeader.priority = static_cast<uint8_t>(priority);
        recHeader.typeSignature = computeTypeSignature<Event>();
        recHeader.checksum = calculateCrc16(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(&event), sizeof(Event)));

        // Write record header
        std::memcpy(&m_buffer[m_offset], &recHeader, sizeof(JournalRecordHeader));
        m_offset += sizeof(JournalRecordHeader);

        // Write event payload
        std::memcpy(&m_buffer[m_offset], &event, sizeof(Event));
        m_offset += sizeof(Event);

        m_recordCount++;
        updateHeaderCount();
        return true;
    }

    /// @brief Number of records written.
    [[nodiscard]] size_t recordCount() const noexcept {
        return m_recordCount;
    }

    /// @brief Total bytes written into buffer (header + all records).
    [[nodiscard]] size_t bytesWritten() const noexcept {
        return m_offset;
    }

    /// @brief Read-only view of the serialized journal data.
    [[nodiscard]] std::span<const uint8_t> data() const noexcept {
        return std::span<const uint8_t>(m_buffer.data(), m_offset);
    }

private:
    void initHeader() noexcept {
        JournalHeader hdr{};
        hdr.magic = CORIUM_JOURNAL_MAGIC;
        hdr.version = CORIUM_JOURNAL_VERSION;
        hdr.schemaHash = computeVariantSchemaHash<EventVariant>();
        hdr.recordCount = 0;
        hdr.reserved = 0;
        std::memcpy(&m_buffer[0], &hdr, sizeof(JournalHeader));
        m_offset = sizeof(JournalHeader);
    }

    void updateHeaderCount() noexcept {
        auto* hdr = reinterpret_cast<JournalHeader*>(&m_buffer[0]);
        hdr->recordCount = static_cast<uint32_t>(m_recordCount);
    }

    std::array<uint8_t, BufferCapacity> m_buffer{};
    size_t m_offset{0};
    size_t m_recordCount{0};
};

/// @ingroup wire
/// @brief Zero-heap event journal reader and deterministic player into Corium EventSinks.
/// @tparam EventVariant Variant of supported event types.
template <typename EventVariant>
class EventJournalReader {
public:
    /// @brief Construct reader over a byte span.
    explicit EventJournalReader(std::span<const uint8_t> journalData) noexcept
        : m_data(journalData) {
        validateAndParseHeader();
    }

    /// @brief Returns true if the journal header is valid (magic, version, schema match).
    [[nodiscard]] bool isValid() const noexcept {
        return m_valid;
    }

    /// @brief Total number of records declared in the header.
    [[nodiscard]] size_t totalRecords() const noexcept {
        return m_valid ? m_header.recordCount : 0;
    }

    /// @brief Rewind playback cursor to the first record.
    void rewind() noexcept {
        m_cursor = sizeof(JournalHeader);
    }

    /// @brief Replay all valid records in the journal directly into an EventSink.
    /// @tparam Sink EventSink or EventBus handle type.
    /// @param sink Target sink to receive replayed events.
    /// @return Number of events successfully replayed.
    template <typename Sink>
    size_t replayInto(Sink& sink) noexcept {
        if (!m_valid) {
            return 0;
        }

        rewind();
        size_t replayed = 0;

        while (m_cursor + sizeof(JournalRecordHeader) <= m_data.size()) {
            JournalRecordHeader recHeader{};
            std::memcpy(&recHeader, &m_data[m_cursor], sizeof(JournalRecordHeader));

            size_t payloadStart = m_cursor + sizeof(JournalRecordHeader);
            if (payloadStart + recHeader.payloadLength > m_data.size()) {
                break; // Truncated record
            }

            // Verify CRC
            uint16_t expectedCrc = calculateCrc16(std::span<const uint8_t>(
                &m_data[payloadStart], recHeader.payloadLength));
            if (recHeader.checksum != expectedCrc) {
                break; // Corrupted record
            }

            // Deserialize and push
            constexpr size_t numTypes = std::variant_size_v<EventVariant>;
            if (recHeader.typeIndex < numTypes) {
                bool pushed = deserializeIndex<Sink>(
                    recHeader,
                    &m_data[payloadStart],
                    sink,
                    std::make_index_sequence<numTypes>{}
                );
                if (pushed) {
                    replayed++;
                }
            }

            m_cursor = payloadStart + recHeader.payloadLength;
        }

        return replayed;
    }

private:
    void validateAndParseHeader() noexcept {
        if (m_data.size() < sizeof(JournalHeader)) {
            m_valid = false;
            return;
        }

        std::memcpy(&m_header, m_data.data(), sizeof(JournalHeader));

        if (m_header.magic != CORIUM_JOURNAL_MAGIC) {
            m_valid = false;
            return;
        }
        if (m_header.version != CORIUM_JOURNAL_VERSION) {
            m_valid = false;
            return;
        }
        if (m_header.schemaHash != computeVariantSchemaHash<EventVariant>()) {
            m_valid = false;
            return;
        }

        m_valid = true;
        m_cursor = sizeof(JournalHeader);
    }

    template <typename Sink, size_t... Is>
    bool deserializeIndex(
        const JournalRecordHeader& recHeader,
        const uint8_t* payload,
        Sink& sink,
        std::index_sequence<Is...>
    ) noexcept {
        bool handled = false;
        (void)((recHeader.typeIndex == Is ? (handled = deserializeExact<Is, Sink>(recHeader, payload, sink), true) : false) || ...);
        return handled;
    }

    template <size_t Index, typename Sink>
    bool deserializeExact(
        const JournalRecordHeader& recHeader,
        const uint8_t* payload,
        Sink& sink
    ) noexcept {
        using TargetEvent = std::variant_alternative_t<Index, EventVariant>;
        if (recHeader.payloadLength != sizeof(TargetEvent)) {
            return false;
        }

        if (recHeader.typeSignature != 0 && recHeader.typeSignature != computeTypeSignature<TargetEvent>()) {
            return false;
        }

        TargetEvent evt{};
        std::memcpy(&evt, payload, sizeof(TargetEvent));
        auto prio = static_cast<EventPriority>(recHeader.priority);
        sink.post(EventVariant{std::move(evt)}, prio);
        return true;
    }

    std::span<const uint8_t> m_data;
    JournalHeader m_header{};
    size_t m_cursor{0};
    bool m_valid{false};
};

} // namespace corium::wire

// <<< End: corium/wire/EventJournal.hpp

// <<< End: corium/wire/wire.hpp

// >>> Begin: corium/profiler/profiler.hpp
/**
 * @file profiler.hpp
 * @ingroup profiler
 * @brief Umbrella header for real-time latency telemetry and flight recorder.
 */



// >>> Begin: corium/profiler/Metrics.hpp
/**
 * @file Metrics.hpp
 * @ingroup profiler
 * @brief Zero-heap Prometheus-compatible metric counters, gauges, and histograms.
 */


#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>

namespace corium::profiler {

/// @ingroup profiler
/// @brief Atomic 64-bit monotonically increasing counter.
class Counter {
public:
    explicit constexpr Counter(std::string_view name, std::string_view help = "") noexcept
        : m_name(name), m_help(help)
    {}

    /// @brief Increment counter value.
    /// @param val Increment amount (default: 1).
    void increment(uint64_t val = 1) noexcept {
        m_value.fetch_add(val, std::memory_order_relaxed);
    }

    /// @brief Current counter value.
    [[nodiscard]] uint64_t get() const noexcept {
        return m_value.load(std::memory_order_relaxed);
    }

    /// @brief Reset counter to zero.
    void reset() noexcept {
        m_value.store(0, std::memory_order_relaxed);
    }

    [[nodiscard]] std::string_view name() const noexcept { return m_name; }
    [[nodiscard]] std::string_view help() const noexcept { return m_help; }

private:
    std::string_view m_name;
    std::string_view m_help;
    std::atomic<uint64_t> m_value{0};
};

/// @ingroup profiler
/// @brief Atomic 64-bit signed gauge metric representing instantaneous level.
class Gauge {
public:
    explicit constexpr Gauge(std::string_view name, std::string_view help = "") noexcept
        : m_name(name), m_help(help)
    {}

    /// @brief Set gauge to an absolute value.
    void set(int64_t val) noexcept {
        m_value.store(val, std::memory_order_relaxed);
    }

    /// @brief Increment gauge.
    void increment(int64_t val = 1) noexcept {
        m_value.fetch_add(val, std::memory_order_relaxed);
    }

    /// @brief Decrement gauge.
    void decrement(int64_t val = 1) noexcept {
        m_value.fetch_sub(val, std::memory_order_relaxed);
    }

    /// @brief Current gauge value.
    [[nodiscard]] int64_t get() const noexcept {
        return m_value.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::string_view name() const noexcept { return m_name; }
    [[nodiscard]] std::string_view help() const noexcept { return m_help; }

private:
    std::string_view m_name;
    std::string_view m_help;
    std::atomic<int64_t> m_value{0};
};

/// @ingroup profiler
/// @brief Statically bucketed latency distribution histogram.
/// @tparam NumBuckets Number of upper boundary buckets (default: 8).
template <size_t NumBuckets = 8>
class Histogram {
public:
    constexpr Histogram(
        std::string_view name,
        std::array<double, NumBuckets> bounds,
        std::string_view help = ""
    ) noexcept
        : m_name(name), m_bounds(bounds), m_help(help)
    {}

    /// @brief Record an observed value.
    /// @param val Observation (e.g. latency in milliseconds or microseconds).
    void observe(double val) noexcept {
        m_count.fetch_add(1, std::memory_order_relaxed);

        for (size_t i = 0; i < NumBuckets; ++i) {
            if (val <= m_bounds[i]) {
                m_buckets[i].fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    [[nodiscard]] uint64_t count() const noexcept {
        return m_count.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t bucketCount(size_t index) const noexcept {
        return index < NumBuckets ? m_buckets[index].load(std::memory_order_relaxed) : 0;
    }

    [[nodiscard]] double bucketBound(size_t index) const noexcept {
        return index < NumBuckets ? m_bounds[index] : 0.0;
    }

    [[nodiscard]] std::string_view name() const noexcept { return m_name; }
    [[nodiscard]] std::string_view help() const noexcept { return m_help; }

private:
    std::string_view m_name;
    std::array<double, NumBuckets> m_bounds;
    std::string_view m_help;
    std::array<std::atomic<uint64_t>, NumBuckets> m_buckets{};
    std::atomic<uint64_t> m_count{0};
};

/// @ingroup profiler
/// @brief Format counter in Prometheus exposition text format into a char buffer.
inline size_t formatPrometheusCounter(const Counter& c, std::span<char> buf) noexcept {
    if (buf.size() < 64) return 0;
    int len = std::snprintf(
        buf.data(), buf.size(),
        "# HELP %.*s %.*s\n# TYPE %.*s counter\n%.*s %lu\n",
        static_cast<int>(c.name().size()), c.name().data(),
        static_cast<int>(c.help().size()), c.help().data(),
        static_cast<int>(c.name().size()), c.name().data(),
        static_cast<int>(c.name().size()), c.name().data(),
        static_cast<unsigned long>(c.get())
    );
    return len > 0 ? static_cast<size_t>(len) : 0;
}

/// @ingroup profiler
/// @brief Format gauge in Prometheus exposition text format into a char buffer.
inline size_t formatPrometheusGauge(const Gauge& g, std::span<char> buf) noexcept {
    if (buf.size() < 64) return 0;
    int len = std::snprintf(
        buf.data(), buf.size(),
        "# HELP %.*s %.*s\n# TYPE %.*s gauge\n%.*s %ld\n",
        static_cast<int>(g.name().size()), g.name().data(),
        static_cast<int>(g.help().size()), g.help().data(),
        static_cast<int>(g.name().size()), g.name().data(),
        static_cast<int>(g.name().size()), g.name().data(),
        static_cast<long>(g.get())
    );
    return len > 0 ? static_cast<size_t>(len) : 0;
}

} // namespace corium::profiler

// <<< End: corium/profiler/Metrics.hpp

// <<< End: corium/profiler/profiler.hpp

// >>> Begin: corium/safety/safety.hpp
/**
 * @file safety.hpp
 * @ingroup safety
 * @brief Umbrella header for safety, supervision, and fault recovery primitives.
 */



// >>> Begin: corium/safety/CircuitBreaker.hpp
/**
 * @file CircuitBreaker.hpp
 * @ingroup safety
 * @brief Lock-free circuit breaker state machine for active fault isolation.
 */


#include <atomic>
#include <chrono>
#include <cstdint>

namespace corium::safety {

/// @brief Circuit Breaker operational state.
enum class CircuitState : uint8_t {
    Closed,   ///< Normal operation: all calls execute.
    Open,     ///< Tripped/Faulty: calls are fast-failed without execution.
    HalfOpen  ///< Recovery probing: allowing a single canary call to verify health.
};

/// @ingroup safety
/// @brief Zero-allocation Circuit Breaker pattern for isolating faulty handlers or peripheral links.
/// Thread-safe lock-free state transitions.
/// @tparam FailureThreshold Consecutive failures before tripping open (default: 3).
/// @tparam RecoveryTimeoutMs Cooldown duration in milliseconds before moving to HalfOpen (default: 500ms).
template <
    uint32_t FailureThreshold = 3,
    uint32_t RecoveryTimeoutMs = 500
>
class CircuitBreaker {
public:
    CircuitBreaker() noexcept = default;

    /// @brief Check if execution is permitted under current circuit state.
    /// Automatically transitions from Open to HalfOpen if recovery cooldown has elapsed.
    [[nodiscard]] bool allowExecution() noexcept
    {
        const CircuitState currentState = _state.load(std::memory_order_acquire);

        if (currentState == CircuitState::Closed) {
            return true;
        }

        if (currentState == CircuitState::Open) {
            const uint64_t now = nowMs();
            const uint64_t trippedAt = _trippedAtMs.load(std::memory_order_acquire);
            if (now >= trippedAt && (now - trippedAt) >= RecoveryTimeoutMs) {
                // Attempt transition to HalfOpen
                CircuitState expected = CircuitState::Open;
                if (_state.compare_exchange_strong(expected, CircuitState::HalfOpen, std::memory_order_acq_rel)) {
                    return true;
                }
            }
            return false;
        }

        // HalfOpen: allow execution for probing
        return true;
    }

    /// @brief Record a successful operation execution.
    /// Resets consecutive failure counter and restores Closed state from HalfOpen.
    void recordSuccess() noexcept
    {
        _failureCount.store(0, std::memory_order_relaxed);
        _state.store(CircuitState::Closed, std::memory_order_release);
    }

    /// @brief Record a failed operation.
    /// Increments failure counter and trips circuit Open if threshold is reached.
    void recordFailure() noexcept
    {
        const uint32_t count = _failureCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count >= FailureThreshold) {
            _trippedAtMs.store(nowMs(), std::memory_order_release);
            _state.store(CircuitState::Open, std::memory_order_release);
        }
    }

    /// @brief Manually reset the circuit breaker to normal Closed state.
    void reset() noexcept
    {
        _failureCount.store(0, std::memory_order_relaxed);
        _trippedAtMs.store(0, std::memory_order_relaxed);
        _state.store(CircuitState::Closed, std::memory_order_release);
    }

    /// @brief Manually trip the circuit breaker open.
    void trip() noexcept
    {
        _trippedAtMs.store(nowMs(), std::memory_order_release);
        _state.store(CircuitState::Open, std::memory_order_release);
    }

    /// @brief Current state of the circuit breaker.
    [[nodiscard]] CircuitState state() const noexcept
    {
        const CircuitState s = _state.load(std::memory_order_acquire);
        if (s == CircuitState::Open) {
            const uint64_t now = nowMs();
            const uint64_t tripped = _trippedAtMs.load(std::memory_order_acquire);
            if (now >= tripped && (now - tripped) >= RecoveryTimeoutMs) {
                return CircuitState::HalfOpen;
            }
        }
        return s;
    }

    /// @brief Current consecutive failure count.
    [[nodiscard]] uint32_t failureCount() const noexcept
    {
        return _failureCount.load(std::memory_order_relaxed);
    }

    /// @brief Execute a protected callable through the circuit breaker.
    /// @tparam Callable Function or lambda returning bool (true = success, false = failure).
    /// @param fn Callable to invoke if circuit is not open.
    /// @return true if executed and succeeded, false if rejected or failed.
    template <typename Callable>
    bool execute(Callable&& fn)
    {
        if (!allowExecution()) {
            return false;
        }

        bool ok = false;
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
        try {
            ok = fn();
        } catch (...) {
            ok = false;
        }
#else
        ok = fn();
#endif

        if (ok) {
            recordSuccess();
        } else {
            recordFailure();
        }

        return ok;
    }

private:
    [[nodiscard]] static uint64_t nowMs() noexcept
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
    }

    std::atomic<CircuitState> _state{CircuitState::Closed};
    std::atomic<uint32_t> _failureCount{0};
    std::atomic<uint64_t> _trippedAtMs{0};
};

} // namespace corium::safety

// <<< End: corium/safety/CircuitBreaker.hpp

// >>> Begin: corium/safety/HeartbeatMonitor.hpp
/**
 * @file HeartbeatMonitor.hpp
 * @ingroup safety
 * @brief Lock-free SLA deadline tracker for multi-service heartbeats.
 */


#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace corium::safety {

/// @ingroup safety
/// @brief Zero-heap multi-service heartbeat tracker for safety-critical execution.
/// Thread-safe for concurrent heartbeat signals and supervisory health checks.
/// @tparam MaxServices Maximum number of monitored services (default: 16).
template <std::size_t MaxServices = 16>
class HeartbeatMonitor {
public:
    struct alignas(32) ServiceEntry {
        uint32_t serviceId{0};
        uint64_t timeoutNs{0};
        std::atomic<uint64_t> lastHeartbeatNs{0};
        std::atomic<bool> active{false};
    };

    HeartbeatMonitor() = default;

    /// @brief Register a service for heartbeat monitoring with a specified timeout.
    /// @param serviceId Unique numeric identifier for the service.
    /// @param timeoutNs Maximum allowed duration between heartbeats in nanoseconds.
    /// @param initialTimestampNs Initial timestamp to baseline the service.
    /// @return true if successfully registered, false if capacity exceeded.
    bool registerService(uint32_t serviceId, uint64_t timeoutNs, uint64_t initialTimestampNs = 0) noexcept
    {
        for (std::size_t i = 0; i < MaxServices; ++i) {
            bool expected = false;
            if (_services[i].active.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
                _services[i].serviceId = serviceId;
                _services[i].timeoutNs = timeoutNs;
                _services[i].lastHeartbeatNs.store(initialTimestampNs, std::memory_order_release);
                return true;
            }
            if (_services[i].serviceId == serviceId && expected) {
                // Already registered; update timeout and baseline
                _services[i].timeoutNs = timeoutNs;
                _services[i].lastHeartbeatNs.store(initialTimestampNs, std::memory_order_release);
                return true;
            }
        }
        return false;
    }

    /// @brief Record a heartbeat for a given service.
    /// @param serviceId Unique service identifier.
    /// @param nowNs Current timestamp in nanoseconds.
    void beat(uint32_t serviceId, uint64_t nowNs) noexcept
    {
        for (std::size_t i = 0; i < MaxServices; ++i) {
            if (_services[i].active.load(std::memory_order_relaxed) && _services[i].serviceId == serviceId) {
                _services[i].lastHeartbeatNs.store(nowNs, std::memory_order_release);
                return;
            }
        }
    }

    /// @brief Unregister a service from active monitoring.
    void unregisterService(uint32_t serviceId) noexcept
    {
        for (std::size_t i = 0; i < MaxServices; ++i) {
            if (_services[i].serviceId == serviceId) {
                _services[i].active.store(false, std::memory_order_release);
                return;
            }
        }
    }

    /// @brief Check health status of all registered active services.
    /// @param nowNs Current timestamp in nanoseconds.
    /// @param outTimedOutServiceId Populated with the ID of the first failing service if unhealthy.
    /// @param outLastHeartbeatNs Populated with the timestamp of the last received heartbeat if unhealthy.
    /// @param outTimeoutBudgetNs Populated with the allowed timeout budget if unhealthy.
    /// @return true if all services are within their heartbeat SLA; false otherwise.
    [[nodiscard]] bool checkHealth(
        uint64_t nowNs,
        uint32_t& outTimedOutServiceId,
        uint64_t& outLastHeartbeatNs,
        uint64_t& outTimeoutBudgetNs
    ) const noexcept
    {
        for (std::size_t i = 0; i < MaxServices; ++i) {
            if (_services[i].active.load(std::memory_order_acquire)) {
                const uint64_t last = _services[i].lastHeartbeatNs.load(std::memory_order_acquire);
                const uint64_t limit = _services[i].timeoutNs;
                if (nowNs > last && (nowNs - last) > limit) {
                    outTimedOutServiceId = _services[i].serviceId;
                    outLastHeartbeatNs = last;
                    outTimeoutBudgetNs = limit;
                    return false;
                }
            }
        }
        return true;
    }

    /// @brief Get maximum capacity of monitored services.
    [[nodiscard]] constexpr std::size_t maxServices() const noexcept
    {
        return MaxServices;
    }

private:
    std::array<ServiceEntry, MaxServices> _services{};
};

} // namespace corium::safety

// <<< End: corium/safety/HeartbeatMonitor.hpp

// >>> Begin: corium/safety/SafetyEvents.hpp
/**
 * @file SafetyEvents.hpp
 * @ingroup safety
 * @brief Heartbeat and fault notification event structures.
 */


#include <cstddef>
#include <cstdint>

namespace corium::safety {

/// @brief Dispatched when a monitored service fails to deliver its heartbeat within its timeout window.
struct WatchdogTimeoutEvent {
    uint32_t serviceId{0};
    uint64_t lastHeartbeatNs{0};
    uint64_t timeoutBudgetNs{0};
};

/// @brief Dispatched when an event handler or task misses its real-time deadline budget.
struct DeadlineMissedEvent {
    std::size_t eventTypeId{0};
    const char* eventName{nullptr};
    double actualDurationUs{0.0};
    double maxAllowedDurationUs{0.0};
};

/// @brief Dispatched when a CircuitBreaker detects repeated failures and trips open.
struct CircuitBreakerTrippedEvent {
    uint32_t componentId{0};
    uint32_t failureCount{0};
    uint64_t recoveryCooldownMs{0};
};

} // namespace corium::safety

// <<< End: corium/safety/SafetyEvents.hpp

// >>> Begin: corium/safety/WatchdogSupervisor.hpp
/**
 * @file WatchdogSupervisor.hpp
 * @ingroup safety
 * @brief Multi-task SLA deadline monitor controlling hardware watchdog refresh.
 */


#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <type_traits>


namespace corium::safety {

/// @ingroup safety
/// @brief Dedicated safety supervisor monitoring subsystem heartbeats and controlling watchdog refresh.
/// Compatible with hardware IWDG (STM32, ESP32, nRF), RTOS Task Watchdogs, and POSIX watchdogs.
/// Zero dynamic heap allocations, zero threading overhead.
/// @tparam MaxServices Maximum number of monitored services.
/// @tparam ClockPolicy Time source policy (defaults to ChronoClockPolicy).
template <
    std::size_t MaxServices = 16,
    typename ClockPolicy = ChronoClockPolicy
>
class WatchdogSupervisor {
public:
    using KickCallbackFn = void (*)(void* userData);

    WatchdogSupervisor() = default;

    /// @brief Configure the physical hardware/software watchdog kick callback.
    /// @param kickFn Function called to refresh the watchdog timer.
    /// @param userData Pointer passed to the kick function.
    void setWatchdogKickCallback(KickCallbackFn kickFn, void* userData = nullptr) noexcept
    {
        _kickFn = kickFn;
        _userData = userData;
    }

    /// @brief Access reference to the internal HeartbeatMonitor.
    [[nodiscard]] HeartbeatMonitor<MaxServices>& heartbeatMonitor() noexcept
    {
        return _monitor;
    }

    /// @brief Access const reference to the internal HeartbeatMonitor.
    [[nodiscard]] const HeartbeatMonitor<MaxServices>& heartbeatMonitor() const noexcept
    {
        return _monitor;
    }

    /// @brief Register a service for supervision.
    bool registerService(uint32_t serviceId, uint64_t timeoutNs) noexcept
    {
        const uint64_t now = nowNs();
        return _monitor.registerService(serviceId, timeoutNs, now);
    }

    /// @brief Submit a heartbeat for a monitored service.
    void beat(uint32_t serviceId) noexcept
    {
        _monitor.beat(serviceId, nowNs());
    }

    /// @brief Perform a supervisor check iteration.
    /// Kicks the hardware watchdog if healthy; suppresses the kick and dispatches WatchdogTimeoutEvent if failed.
    /// @tparam EventSinkType Event sink type to post failure events.
    /// @param sink Event sink handle to dispatch emergency events.
    /// @return true if all services were healthy and watchdog was kicked; false if watchdog was suppressed.
    template <typename EventSinkType>
    bool supervise(const EventSinkType& sink) noexcept
    {
        const uint64_t now = nowNs();
        uint32_t timedOutId = 0;
        uint64_t lastBeat = 0;
        uint64_t budget = 0;

        if (_monitor.checkHealth(now, timedOutId, lastBeat, budget)) {
            // System is healthy: feed the watchdog
            if (_kickFn) {
                _kickFn(_userData);
            }
            _kicksCount.fetch_add(1, std::memory_order_relaxed);
            _lastHealthyTimeNs.store(now, std::memory_order_release);
            return true;
        }

        // Failure detected: suppress watchdog kick and post emergency event
        _suppressionsCount.fetch_add(1, std::memory_order_relaxed);
        sink.postHighPriority(WatchdogTimeoutEvent{.serviceId = timedOutId, .lastHeartbeatNs = lastBeat, .timeoutBudgetNs = budget});
        return false;
    }

    /// @brief Get total number of successful watchdog kicks.
    [[nodiscard]] uint64_t totalKicks() const noexcept
    {
        return _kicksCount.load(std::memory_order_relaxed);
    }

    /// @brief Get total number of watchdog kick suppressions due to health violations.
    [[nodiscard]] uint64_t totalSuppressions() const noexcept
    {
        return _suppressionsCount.load(std::memory_order_relaxed);
    }

private:
    [[nodiscard]] static uint64_t nowNs() noexcept
    {
        if constexpr (std::is_integral_v<typename ClockPolicy::time_point>) {
            return static_cast<uint64_t>(ClockPolicy::now());
        } else {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    ClockPolicy::now().time_since_epoch()
                ).count()
            );
        }
    }

    HeartbeatMonitor<MaxServices> _monitor{};
    KickCallbackFn _kickFn{nullptr};
    void* _userData{nullptr};
    std::atomic<uint64_t> _kicksCount{0};
    std::atomic<uint64_t> _suppressionsCount{0};
    std::atomic<uint64_t> _lastHealthyTimeNs{0};
};

} // namespace corium::safety

// <<< End: corium/safety/WatchdogSupervisor.hpp

// >>> Begin: corium/safety/WatchdogService.hpp
/**
 * @file WatchdogService.hpp
 * @ingroup safety
 * @brief Autonomous background service for hardware watchdog supervision.
 */


#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <thread>


namespace corium::safety {

/// @ingroup safety
/// @brief Multi-threaded background safety service executing periodic heartbeat supervision.
/// Automatically verifies monitored task deadlines and dispatches WatchdogTimeoutEvent to the main event bus on violations.
/// @tparam EventVariant Supported event variant list type.
/// @tparam MaxServices Maximum number of monitored services (default 16).
/// @tparam ClockPolicy Clock source strategy (defaults to ChronoClockPolicy).
template <
    typename EventVariant = DefaultEvents,
    std::size_t MaxServices = 16,
    typename ClockPolicy = ChronoClockPolicy
>
class WatchdogService : public BackgroundService<EventVariant> {
public:
    using SupervisorType = WatchdogSupervisor<MaxServices, ClockPolicy>;
    using KickCallbackFn = typename SupervisorType::KickCallbackFn;

    explicit WatchdogService(std::chrono::milliseconds checkInterval = std::chrono::milliseconds(20))
        : _checkInterval(checkInterval)
    {}

    /// @brief Set callback to refresh the physical hardware watchdog (e.g. STM32 IWDG).
    void setWatchdogKickCallback(KickCallbackFn kickFn, void* userData = nullptr) noexcept
    {
        _supervisor.setWatchdogKickCallback(kickFn, userData);
    }

    /// @brief Register a service for supervision with maximum timeout in nanoseconds.
    bool registerService(uint32_t serviceId, uint64_t timeoutNs) noexcept
    {
        return _supervisor.registerService(serviceId, timeoutNs);
    }

    /// @brief Submit a heartbeat for a monitored service.
    void beat(uint32_t serviceId) noexcept
    {
        _supervisor.beat(serviceId);
    }

    /// @brief Access underlying WatchdogSupervisor.
    [[nodiscard]] SupervisorType& supervisor() noexcept
    {
        return _supervisor;
    }

    [[nodiscard]] const SupervisorType& supervisor() const noexcept
    {
        return _supervisor;
    }

    /// @brief Background worker loop executing periodic health supervision.
    void run(const std::stop_token& stopToken)
    {
        while (!stopToken.stop_requested()) {
            std::this_thread::sleep_for(_checkInterval);
            _supervisor.supervise(this->context().mainSink());
        }
    }

private:
    std::chrono::milliseconds _checkInterval;
    SupervisorType _supervisor;
};

} // namespace corium::safety

// <<< End: corium/safety/WatchdogService.hpp

// <<< End: corium/safety/safety.hpp

// >>> Begin: corium/ipc/ipc.hpp
/**
 * @file ipc.hpp
 * @ingroup ipc
 * @brief Umbrella header for inter-process communication primitives.
 */



// >>> Begin: corium/ipc/DomainSocket.hpp
/**
 * @file DomainSocket.hpp
 * @ingroup ipc
 * @brief UNIX Domain Socket datagram listener and client implementation.
 */


#include <cstddef>
#include <cstring>
#include <string>
#include <utility>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <afunix.h>
#define CORIUM_HAS_POSIX_SOCKETS 0
#elif __has_include(<sys/socket.h>) && __has_include(<sys/un.h>) && __has_include(<unistd.h>) && __has_include(<fcntl.h>)
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#define CORIUM_HAS_POSIX_SOCKETS 1
#else
#define CORIUM_HAS_POSIX_SOCKETS 0
#endif

namespace corium::ipc {

/// @brief Low-level RAII abstraction for UNIX Domain Datagram Sockets (AF_UNIX / SOCK_DGRAM).
/// Provides discrete, boundary-preserving, zero-fragmentation message passing.
class DomainSocket {
public:
    DomainSocket() noexcept = default;

    ~DomainSocket()
    {
        close();
    }

    DomainSocket(const DomainSocket&) = delete;
    DomainSocket& operator=(const DomainSocket&) = delete;

    DomainSocket(DomainSocket&& other) noexcept
        : _fd(other._fd),
          _boundPath(std::move(other._boundPath)),
          _isBound(other._isBound)
    {
        other._fd = -1;
        other._isBound = false;
    }

    DomainSocket& operator=(DomainSocket&& other) noexcept
    {
        if (this != &other) {
            close();
            _fd = other._fd;
            _boundPath = std::move(other._boundPath);
            _isBound = other._isBound;
            other._fd = -1;
            other._isBound = false;
        }
        return *this;
    }

    /// @brief Bind socket to a filesystem path as a receiving server.
    /// @param socketPath Filesystem path for the socket (e.g. "/tmp/corium_daemon.sock").
    /// @return true on success, false otherwise.
    bool bind(const std::string& socketPath) noexcept
    {
        close();

#if CORIUM_HAS_POSIX_SOCKETS
        ::unlink(socketPath.c_str());

        _fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
        if (_fd < 0) {
            return false;
        }

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

        if (::bind(_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(_fd);
            _fd = -1;
            return false;
        }

        _boundPath = socketPath;
        _isBound = true;
        return true;
#else
        (void)socketPath;
        return false;
#endif
    }

    /// @brief Connect to a destination socket as a client.
    /// @param socketPath Filesystem path of the target server socket.
    /// @param clientPath Optional path to bind this client for bidirectional responses.
    /// @return true on success, false otherwise.
    bool connect(const std::string& socketPath, const std::string& clientPath = "") noexcept
    {
        close();

#if CORIUM_HAS_POSIX_SOCKETS
        _fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
        if (_fd < 0) {
            return false;
        }

        if (!clientPath.empty()) {
            ::unlink(clientPath.c_str());
            struct sockaddr_un localAddr{};
            localAddr.sun_family = AF_UNIX;
            std::strncpy(localAddr.sun_path, clientPath.c_str(), sizeof(localAddr.sun_path) - 1);

            if (::bind(_fd, reinterpret_cast<struct sockaddr*>(&localAddr), sizeof(localAddr)) != 0) {
                ::close(_fd);
                _fd = -1;
                return false;
            }
            _boundPath = clientPath;
            _isBound = true;
        }

        struct sockaddr_un remoteAddr{};
        remoteAddr.sun_family = AF_UNIX;
        std::strncpy(remoteAddr.sun_path, socketPath.c_str(), sizeof(remoteAddr.sun_path) - 1);

        if (::connect(_fd, reinterpret_cast<struct sockaddr*>(&remoteAddr), sizeof(remoteAddr)) != 0) {
            close();
            return false;
        }
        return true;
#else
        (void)socketPath;
        (void)clientPath;
        return false;
#endif
    }

    /// @brief Set socket non-blocking mode.
    void setNonBlocking(bool nonBlocking) noexcept
    {
#if CORIUM_HAS_POSIX_SOCKETS
        if (_fd >= 0) {
            int flags = ::fcntl(_fd, F_GETFL, 0);
            if (flags >= 0) {
                if (nonBlocking) {
                    flags |= O_NONBLOCK;
                } else {
                    flags &= ~O_NONBLOCK;
                }
                ::fcntl(_fd, F_SETFL, flags);
            }
        }
#else
        (void)nonBlocking;
#endif
    }

    /// @brief Send datagram packet over the connected socket.
    /// @param buffer Pointer to payload.
    /// @param length Payload length in bytes.
    /// @return Number of bytes sent, or -1 on error.
    int send(const void* buffer, std::size_t length) noexcept
    {
#if CORIUM_HAS_POSIX_SOCKETS
        if (_fd < 0) return -1;
        return static_cast<int>(::send(_fd, buffer, length, 0));
#else
        (void)buffer;
        (void)length;
        return -1;
#endif
    }

    /// @brief Receive datagram packet from socket.
    /// @param buffer Destination buffer.
    /// @param maxLength Maximum bytes to receive.
    /// @return Number of bytes received, or -1 on error/EWOULDBLOCK.
    int receive(void* buffer, std::size_t maxLength) noexcept
    {
#if CORIUM_HAS_POSIX_SOCKETS
        if (_fd < 0) return -1;
        return static_cast<int>(::recv(_fd, buffer, maxLength, 0));
#else
        (void)buffer;
        (void)maxLength;
        return -1;
#endif
    }

    /// @brief Close socket and unlink bound file if server.
    void close() noexcept
    {
#if CORIUM_HAS_POSIX_SOCKETS
        if (_fd >= 0) {
            ::close(_fd);
            _fd = -1;
        }
        if (_isBound && !_boundPath.empty()) {
            ::unlink(_boundPath.c_str());
            _isBound = false;
            _boundPath.clear();
        }
#endif
    }

    /// @brief Check if socket descriptor is open.
    [[nodiscard]] bool isOpen() const noexcept { return _fd >= 0; }

    /// @brief Access underlying OS file descriptor.
    [[nodiscard]] int nativeHandle() const noexcept { return _fd; }

private:
    int _fd{-1};
    std::string _boundPath;
    bool _isBound{false};
};

} // namespace corium::ipc

// <<< End: corium/ipc/DomainSocket.hpp

// >>> Begin: corium/ipc/IpcChannel.hpp
/**
 * @file IpcChannel.hpp
 * @ingroup ipc
 * @brief Zero-copy typed event exchange over POSIX shared memory.
 */


#include <cstddef>
#include <string>
#include <utility>


// >>> Begin: corium/ipc/SharedMemory.hpp
/**
 * @file SharedMemory.hpp
 * @ingroup ipc
 * @brief RAII wrapper for POSIX shared memory and Windows file mappings.
 */


#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define CORIUM_HAS_POSIX_SHM 0
#elif __has_include(<sys/mman.h>) && __has_include(<unistd.h>) && __has_include(<fcntl.h>)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define CORIUM_HAS_POSIX_SHM 1
#else
#define CORIUM_HAS_POSIX_SHM 0
#endif

namespace corium::ipc {

/// @ingroup ipc
/// @brief Non-allocating wrapper for fixed raw memory regions (e.g. multi-core SRAM, DMA buffers, hardware shared RAM).
class RawMemoryBuffer {
public:
    constexpr RawMemoryBuffer() noexcept = default;

    constexpr RawMemoryBuffer(void* address, std::size_t size, bool isCreator = false) noexcept
        : _address(address), _size(size), _isCreator(isCreator)
    {}

    [[nodiscard]] void* data() noexcept { return _address; }
    [[nodiscard]] const void* data() const noexcept { return _address; }
    [[nodiscard]] std::size_t size() const noexcept { return _size; }
    [[nodiscard]] bool isValid() const noexcept { return _address != nullptr; }
    [[nodiscard]] bool isCreator() const noexcept { return _isCreator; }

private:
    void* _address{nullptr};
    std::size_t _size{0};
    bool _isCreator{false};
};

/// @ingroup ipc
/// @brief Cross-platform zero-copy shared memory region wrapper.
/// Manages OS-level shared memory allocation, memory mapping, and cleanup.
class SharedMemory {
public:
    enum class AccessMode : uint8_t {
        CreateOrOpen,
        OpenReadOnly,
        OpenReadWrite
    };

    SharedMemory() noexcept = default;

    ~SharedMemory()
    {
        close();
    }

    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    SharedMemory(SharedMemory&& other) noexcept
        : _name(std::move(other._name)),
          _address(other._address),
          _size(other._size),
          _isCreator(other._isCreator)
#if defined(_WIN32) || defined(_WIN64)
        , _handle(other._handle)
#elif CORIUM_HAS_POSIX_SHM
        , _fd(other._fd)
#endif
    {
        other._address = nullptr;
        other._size = 0;
        other._isCreator = false;
#if defined(_WIN32) || defined(_WIN64)
        other._handle = nullptr;
#elif CORIUM_HAS_POSIX_SHM
        other._fd = -1;
#endif
    }

    SharedMemory& operator=(SharedMemory&& other) noexcept
    {
        if (this != &other) {
            close();
            _name = std::move(other._name);
            _address = other._address;
            _size = other._size;
            _isCreator = other._isCreator;
#if defined(_WIN32) || defined(_WIN64)
            _handle = other._handle;
            other._handle = nullptr;
#elif CORIUM_HAS_POSIX_SHM
            _fd = other._fd;
            other._fd = -1;
#endif
            other._address = nullptr;
            other._size = 0;
            other._isCreator = false;
        }
        return *this;
    }

    /// @brief Create or attach to a shared memory region.
    /// @param name Unique system identifier (e.g., "/corium_telemetry_shm").
    /// @param size Required memory segment capacity in bytes.
    /// @param mode Allocation/Access mode.
    /// @return true on success, false otherwise.
    bool open(const std::string& name, std::size_t size, AccessMode mode = AccessMode::CreateOrOpen) noexcept
    {
        close();
        _name = normalizeName(name);
        _size = size;

#if defined(_WIN32) || defined(_WIN64)
        DWORD protect = (mode == AccessMode::OpenReadOnly) ? PAGE_READONLY : PAGE_READWRITE;
        DWORD desiredAccess = (mode == AccessMode::OpenReadOnly) ? FILE_MAP_READ : FILE_MAP_ALL_ACCESS;

        if (mode == AccessMode::CreateOrOpen) {
            _handle = CreateFileMappingA(
                INVALID_HANDLE_VALUE,
                nullptr,
                protect,
                static_cast<DWORD>(size >> 32),
                static_cast<DWORD>(size & 0xFFFFFFFF),
                _name.c_str()
            );
            if (_handle) {
                _isCreator = (GetLastError() != ERROR_ALREADY_EXISTS);
            }
        } else {
            _handle = OpenFileMappingA(desiredAccess, FALSE, _name.c_str());
            _isCreator = false;
        }

        if (!_handle) {
            return false;
        }

        _address = MapViewOfFile(_handle, desiredAccess, 0, 0, size);
        if (!_address) {
            CloseHandle(_handle);
            _handle = nullptr;
            return false;
        }
        return true;
#elif CORIUM_HAS_POSIX_SHM
        int oflag = 0;
        mode_t permissions = 0666;

        if (mode == AccessMode::CreateOrOpen) {
            oflag = O_CREAT | O_RDWR;
        } else if (mode == AccessMode::OpenReadOnly) {
            oflag = O_RDONLY;
        } else {
            oflag = O_RDWR;
        }

        _fd = ::shm_open(_name.c_str(), oflag, permissions);
        if (_fd < 0) {
            return false;
        }

        if (mode == AccessMode::CreateOrOpen) {
            struct stat sb{};
            if (::fstat(_fd, &sb) == 0 && sb.st_size < static_cast<off_t>(size)) {
                if (::ftruncate(_fd, static_cast<off_t>(size)) != 0) {
                    ::close(_fd);
                    _fd = -1;
                    return false;
                }
                _isCreator = true;
            }
        }

        int prot = (mode == AccessMode::OpenReadOnly) ? PROT_READ : (PROT_READ | PROT_WRITE);
        _address = ::mmap(nullptr, size, prot, MAP_SHARED, _fd, 0);
        if (_address == MAP_FAILED) {
            _address = nullptr;
            ::close(_fd);
            _fd = -1;
            return false;
        }
        return true;
#else
        (void)mode;
        return false;
#endif
    }

    /// @brief Close and unmap the shared memory segment.
    void close() noexcept
    {
        if (_address) {
#if defined(_WIN32) || defined(_WIN64)
            UnmapViewOfFile(_address);
            if (_handle) {
                CloseHandle(_handle);
                _handle = nullptr;
            }
#elif CORIUM_HAS_POSIX_SHM
            ::munmap(_address, _size);
            if (_fd >= 0) {
                ::close(_fd);
                _fd = -1;
            }
#endif
            _address = nullptr;
            _size = 0;
            _isCreator = false;
        }
    }

    /// @brief Remove shared memory identifier from OS namespace (POSIX shm_unlink).
    static void unlink(const std::string& name) noexcept
    {
#if CORIUM_HAS_POSIX_SHM
        std::string n = normalizeName(name);
        ::shm_unlink(n.c_str());
#else
        (void)name;
#endif
    }

    /// @brief Raw pointer to mapped shared memory buffer.
    [[nodiscard]] void* data() noexcept { return _address; }
    [[nodiscard]] const void* data() const noexcept { return _address; }

    /// @brief Size of mapped shared memory buffer.
    [[nodiscard]] std::size_t size() const noexcept { return _size; }

    /// @brief Check if mapped region is valid.
    [[nodiscard]] bool isValid() const noexcept { return _address != nullptr; }

    /// @brief Check if this process created the memory segment (useful for initialization).
    [[nodiscard]] bool isCreator() const noexcept { return _isCreator; }

    /// @brief Name identifier of shared memory.
    [[nodiscard]] const std::string& name() const noexcept { return _name; }

private:
    static std::string normalizeName(const std::string& name)
    {
#if !defined(_WIN32) && !defined(_WIN64)
        if (name.empty() || name[0] != '/') {
            return "/" + name;
        }
#endif
        return name;
    }

    std::string _name;
    void* _address{nullptr};
    std::size_t _size{0};
    bool _isCreator{false};

#if defined(_WIN32) || defined(_WIN64)
    HANDLE _handle{nullptr};
#elif CORIUM_HAS_POSIX_SHM
    int _fd{-1};
#endif
};

} // namespace corium::ipc

// <<< End: corium/ipc/SharedMemory.hpp

// >>> Begin: corium/ipc/ShmMpscQueue.hpp
/**
 * @file ShmMpscQueue.hpp
 * @ingroup ipc
 * @brief Lock-free multi-producer single-consumer queue located in shared memory.
 */


#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace corium::ipc {

constexpr uint32_t CORIUM_SHM_MAGIC = 0x434F5249; // "CORI"
constexpr uint32_t CORIUM_SHM_VERSION = 1;

/// @brief Lock-free, zero-allocation multi-producer single-consumer ring buffer layout for shared memory.
/// Compatible with POD and trivially copyable types (or serialized payloads).
/// @tparam T Value type stored in each ring cell.
/// @tparam Capacity Number of slots (must be a power of 2).
template <typename T, std::size_t Capacity = 256>
class ShmMpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert(std::is_trivially_copyable_v<T>, "ShmMpscQueue value type T must be trivially copyable for shared memory safety.");

public:
    static constexpr std::size_t BufferCapacity = Capacity;
    static constexpr std::size_t BufferMask = Capacity - 1;

    struct Cell {
        alignas(64) std::atomic<std::size_t> sequence{0};
        alignas(alignof(T)) uint8_t storage[sizeof(T)]{};

        [[nodiscard]] T* ptr() noexcept
        {
            return reinterpret_cast<T*>(storage);
        }

        [[nodiscard]] const T* ptr() const noexcept
        {
            return reinterpret_cast<const T*>(storage);
        }
    };

    struct Layout {
        uint32_t magic{CORIUM_SHM_MAGIC};
        uint32_t version{CORIUM_SHM_VERSION};
        uint32_t capacity{Capacity};
        uint32_t elementSize{sizeof(T)};

        alignas(64) std::atomic<std::size_t> enqueuePos{0};
        alignas(64) std::atomic<std::size_t> dequeuePos{0};

        Cell cells[Capacity];
    };

    ShmMpscQueue() = default;

    /// @brief Construct queue bound to a mapped shared memory address.
    explicit ShmMpscQueue(void* mappedAddress, bool initializeMemory = false) noexcept
    {
        bind(mappedAddress, initializeMemory);
    }

    /// @brief Bind to a mapped shared memory region.
    /// @param mappedAddress Pointer to shared memory.
    /// @param initializeMemory If true, resets header and cell sequences (creator process).
    void bind(void* mappedAddress, bool initializeMemory = false) noexcept
    {
        _layout = static_cast<Layout*>(mappedAddress);
        if (_layout && initializeMemory) {
            _layout->magic = CORIUM_SHM_MAGIC;
            _layout->version = CORIUM_SHM_VERSION;
            _layout->capacity = Capacity;
            _layout->elementSize = sizeof(T);
            _layout->enqueuePos.store(0, std::memory_order_relaxed);
            _layout->dequeuePos.store(0, std::memory_order_relaxed);

            for (std::size_t i = 0; i < Capacity; ++i) {
                _layout->cells[i].sequence.store(i, std::memory_order_relaxed);
            }
        }
    }

    /// @brief Required byte size of the shared memory layout.
    [[nodiscard]] static constexpr std::size_t requiredMemorySize() noexcept
    {
        return sizeof(Layout);
    }

    /// @brief Validate that the mapped shared memory contains a compatible ShmMpscQueue header.
    [[nodiscard]] bool isValid() const noexcept
    {
        if (!_layout) {
            return false;
        }
        return _layout->magic == CORIUM_SHM_MAGIC &&
               _layout->version == CORIUM_SHM_VERSION &&
               _layout->capacity == Capacity &&
               _layout->elementSize == sizeof(T);
    }

    /// @brief Lock-free push into shared memory queue (multi-producer safe).
    /// @param item Value to push.
    /// @return true if pushed, false if queue is full.
    template <typename U>
    bool tryPush(U&& item) noexcept
    {
        if (!_layout) {
            return false;
        }

        Cell* cell = nullptr;
        std::size_t pos = _layout->enqueuePos.load(std::memory_order_relaxed);

        for (;;) {
            cell = &_layout->cells[pos & BufferMask];
            const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (dif == 0) {
                if (_layout->enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (dif < 0) {
                // Buffer is full
                return false;
            } else {
                pos = _layout->enqueuePos.load(std::memory_order_relaxed);
            }
        }

        // Construct / copy item into cell storage
        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memcpy(cell->storage, &item, sizeof(T));
        } else {
            new (cell->storage) T(std::forward<U>(item));
        }

        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    /// @brief Lock-free pop from shared memory queue (single-consumer safe).
    /// @param outItem Reference populated with popped value.
    /// @return true if popped, false if queue is empty.
    bool tryPop(T& outItem) noexcept
    {
        if (!_layout) {
            return false;
        }

        std::size_t pos = _layout->dequeuePos.load(std::memory_order_relaxed);
        Cell* cell = &_layout->cells[pos & BufferMask];
        const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
        const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

        if (dif < 0) {
            // Buffer is empty
            return false;
        }

        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memcpy(&outItem, cell->storage, sizeof(T));
        } else {
            outItem = std::move(*cell->ptr());
            cell->ptr()->~T();
        }

        cell->sequence.store(pos + Capacity, std::memory_order_release);
        _layout->dequeuePos.store(pos + 1, std::memory_order_relaxed);
        return true;
    }

    /// @brief Check if queue is currently empty.
    [[nodiscard]] bool empty() const noexcept
    {
        if (!_layout) return true;
        const std::size_t pos = _layout->dequeuePos.load(std::memory_order_relaxed);
        const Cell* cell = &_layout->cells[pos & BufferMask];
        const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
        return static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1) < 0;
    }

    /// @brief Get configured capacity of the queue.
    [[nodiscard]] constexpr std::size_t capacity() const noexcept
    {
        return Capacity;
    }

private:
    Layout* _layout{nullptr};
};

} // namespace corium::ipc

// <<< End: corium/ipc/ShmMpscQueue.hpp

namespace corium::ipc {

/// @ingroup ipc
/// @brief High-level typed inter-process communication channel for Corium events.
/// Encapsulates OS shared memory allocation and lock-free event dispatching.
/// @tparam EventVariant Supported event variant list (must be trivially copyable or POD).
/// @tparam Capacity Ring buffer capacity (power of 2, default 256).
template <
    typename EventVariant = DefaultEvents,
    std::size_t Capacity = 256
>
class IpcChannel {
    static_assert(std::is_trivially_copyable_v<EventVariant>,
        "IpcChannel EventVariant must be trivially copyable for zero-copy shared memory IPC.");

public:
    using QueueType = ShmMpscQueue<EventVariant, Capacity>;

    IpcChannel() = default;

    /// @brief Create a new shared memory channel as the host/creator process.
    /// @param channelName Unique system name (e.g. "/corium_robot_telemetry").
    /// @return true on success, false otherwise.
    bool create(const std::string& channelName) noexcept
    {
        if (!_shm.open(channelName, QueueType::requiredMemorySize(), SharedMemory::AccessMode::CreateOrOpen)) {
            return false;
        }
        _queue.bind(_shm.data(), true);
        return _queue.isValid();
    }

    /// @brief Attach to an existing shared memory channel as a client process.
    /// @param channelName Unique system name.
    /// @return true on success, false otherwise.
    bool attach(const std::string& channelName) noexcept
    {
        if (!_shm.open(channelName, QueueType::requiredMemorySize(), SharedMemory::AccessMode::OpenReadWrite)) {
            return false;
        }
        _queue.bind(_shm.data(), false);
        return _queue.isValid();
    }

    /// @brief Bind channel directly to a raw memory buffer (e.g. multi-core embedded SRAM).
    /// @param buffer Pointer to raw memory region.
    /// @param isCreator If true, initializes queue headers; if false, attaches to existing queue.
    /// @return true if valid, false if buffer is null.
    bool bindRaw(void* buffer, bool isCreator = true) noexcept
    {
        _queue.bind(buffer, isCreator);
        return _queue.isValid();
    }

    /// @brief Post an event into the shared memory queue for remote processes.
    /// Lock-free, zero-allocation, multi-producer safe.
    /// @tparam EventType Strongly-typed event type.
    /// @param event Event payload.
    /// @return true if pushed, false if shared queue is full.
    template <typename EventType>
    bool post(EventType&& event) noexcept
    {
        return _queue.tryPush(EventVariant{std::forward<EventType>(event)});
    }

    /// @brief Pop one event from the shared queue.
    /// Single-consumer safe.
    /// @param outEvent Reference populated with popped event variant.
    /// @return true if an event was popped, false if queue is empty.
    bool tryPop(EventVariant& outEvent) noexcept
    {
        return _queue.tryPop(outEvent);
    }

    /// @brief Drain incoming shared memory events into a target event sink.
    /// @tparam SinkType Target EventSink or compatible sink.
    /// @param sink Target sink instance.
    /// @param maxEvents Maximum events to drain (0 = drain all pending).
    /// @return Number of events successfully transferred into local runtime.
    template <typename SinkType>
    std::size_t pumpInto(const SinkType& sink, std::size_t maxEvents = 0)
    {
        std::size_t count = 0;
        EventVariant ev;
        while (_queue.tryPop(ev)) {
            sink.post(std::move(ev));
            count++;
            if (maxEvents > 0 && count >= maxEvents) {
                break;
            }
        }
        return count;
    }

    /// @brief Destroy the channel and remove shared memory from OS.
    void unlink() noexcept
    {
        if (_shm.isValid()) {
            std::string name = _shm.name();
            _shm.close();
            SharedMemory::unlink(name);
        }
    }

    /// @brief Check if channel is open and valid.
    [[nodiscard]] bool isValid() const noexcept
    {
        return _shm.isValid() && _queue.isValid();
    }

    /// @brief Check if channel is empty.
    [[nodiscard]] bool empty() const noexcept
    {
        return _queue.empty();
    }

    /// @brief Configured capacity of the channel.
    [[nodiscard]] constexpr std::size_t capacity() const noexcept
    {
        return Capacity;
    }

private:
    SharedMemory _shm;
    QueueType _queue;
};

} // namespace corium::ipc

// <<< End: corium/ipc/IpcChannel.hpp

// >>> Begin: corium/ipc/UdsChannel.hpp
/**
 * @file UdsChannel.hpp
 * @ingroup ipc
 * @brief UNIX Domain Socket datagram inter-process event channel.
 */


#include <cstddef>
#include <cstring>
#include <string>
#include <utility>


namespace corium::ipc {

/// @ingroup ipc
/// @brief Typed IPC channel operating over UNIX Domain Datagram Sockets.
/// Provides boundary-preserving, discrete event transmission with zero packet fragmentation.
/// @tparam EventVariant Supported event variant list (must be trivially copyable or POD).
/// @tparam MaxPacketSize Maximum serialized datagram packet buffer size (default 512 bytes).
template <
    typename EventVariant = DefaultEvents,
    std::size_t MaxPacketSize = 512
>
class UdsChannel {
    static_assert(sizeof(EventVariant) <= MaxPacketSize, "EventVariant size exceeds MaxPacketSize");
    static_assert(std::is_trivially_copyable_v<EventVariant>,
        "UdsChannel EventVariant must be trivially copyable for datagram socket transmission.");

public:
    UdsChannel() = default;

    /// @brief Start listening as an IPC server on a filesystem socket path.
    /// @param socketPath Filesystem path (e.g. "/tmp/corium_control.sock").
    /// @param nonBlocking If true, enables non-blocking mode for non-waiting event loop polling.
    /// @return true on success, false otherwise.
    bool listen(const std::string& socketPath, bool nonBlocking = true) noexcept
    {
        if (!_socket.bind(socketPath)) {
            return false;
        }
        if (nonBlocking) {
            _socket.setNonBlocking(true);
        }
        return true;
    }

    /// @brief Connect as a client to a server socket path.
    /// @param serverPath Server destination socket path.
    /// @param clientPath Optional client socket path for bidirectional reply.
    /// @return true on success, false otherwise.
    bool connect(const std::string& serverPath, const std::string& clientPath = "") noexcept
    {
        return _socket.connect(serverPath, clientPath);
    }

    /// @brief Post a typed event over the socket to the remote receiver.
    /// @tparam EventType Event type.
    /// @param event Event payload.
    /// @return true if datagram sent, false on socket error.
    template <typename EventType>
    bool post(EventType&& event) noexcept
    {
        EventVariant ev{std::forward<EventType>(event)};
        const int bytesSent = _socket.send(&ev, sizeof(EventVariant));
        return bytesSent == static_cast<int>(sizeof(EventVariant));
    }

    /// @brief Try popping one pending datagram event from the socket (non-blocking).
    /// @param outEvent Reference populated with received event variant.
    /// @return true if an event was received, false if socket has no pending datagrams.
    bool tryPop(EventVariant& outEvent) noexcept
    {
        const int bytesReceived = _socket.receive(&outEvent, sizeof(EventVariant));
        return bytesReceived == static_cast<int>(sizeof(EventVariant));
    }

    /// @brief Drain incoming UNIX domain socket events into a target event sink.
    /// @tparam SinkType Target EventSink or compatible sink.
    /// @param sink Target sink instance.
    /// @param maxEvents Maximum events to drain (0 = drain all pending).
    /// @return Number of events successfully transferred into local runtime.
    template <typename SinkType>
    std::size_t pumpInto(const SinkType& sink, std::size_t maxEvents = 0)
    {
        std::size_t count = 0;
        EventVariant ev;
        while (tryPop(ev)) {
            sink.post(std::move(ev));
            count++;
            if (maxEvents > 0 && count >= maxEvents) {
                break;
            }
        }
        return count;
    }

    /// @brief Close the socket connection and unlink socket file if server.
    void close() noexcept
    {
        _socket.close();
    }

    /// @brief Check if socket is open.
    [[nodiscard]] bool isOpen() const noexcept
    {
        return _socket.isOpen();
    }

    /// @brief Access underlying DomainSocket instance.
    [[nodiscard]] DomainSocket& socket() noexcept
    {
        return _socket;
    }

private:
    DomainSocket _socket;
};

} // namespace corium::ipc

// <<< End: corium/ipc/UdsChannel.hpp

// >>> Begin: corium/ipc/PlatformChannel.hpp
/**
 * @file PlatformChannel.hpp
 * @ingroup ipc
 * @brief Cross-platform portable IPC channel alias.
 */



namespace corium::ipc {

/// @ingroup ipc
/// @brief Platform-agnostic inter-process datagram channel.
/// Resolves to UdsChannel on POSIX/UNIX systems and supported Windows platforms.
template <typename EventVariant>
using PlatformChannel = UdsChannel<EventVariant>;

} // namespace corium::ipc

// <<< End: corium/ipc/PlatformChannel.hpp

// <<< End: corium/ipc/ipc.hpp

// >>> Begin: corium/net/net.hpp
/**
 * @file net.hpp
 * @ingroup net
 * @brief Umbrella header for zero-heap networking channels and protocols.
 */



// >>> Begin: corium/net/StaticUdpChannel.hpp
/**
 * @file StaticUdpChannel.hpp
 * @ingroup net
 * @brief Zero-heap UDP network channel for distributed event telemetry and IoT nodes.
 */


#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#define CORIUM_HAS_UDP_SOCKETS 1
#elif __has_include(<sys/socket.h>) && __has_include(<netinet/in.h>) && __has_include(<arpa/inet.h>) && __has_include(<unistd.h>) && __has_include(<fcntl.h>)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define CORIUM_HAS_UDP_SOCKETS 1
#else
#define CORIUM_HAS_UDP_SOCKETS 0
#endif


namespace corium::net {

/// @ingroup net
/// @brief Statically buffered, zero-heap UDP communication channel for distributed Corium nodes.
/// @tparam MaxPacketSize Maximum UDP datagram payload size in bytes (default: 512).
template <size_t MaxPacketSize = 512>
class StaticUdpChannel {
public:
    constexpr StaticUdpChannel() noexcept = default;

    ~StaticUdpChannel() {
        close();
    }

    StaticUdpChannel(const StaticUdpChannel&) = delete;
    StaticUdpChannel& operator=(const StaticUdpChannel&) = delete;

    StaticUdpChannel(StaticUdpChannel&& other) noexcept
        : m_fd(other.m_fd) {
        other.m_fd = -1;
    }

    StaticUdpChannel& operator=(StaticUdpChannel&& other) noexcept {
        if (this != &other) {
            close();
            m_fd = other.m_fd;
            other.m_fd = -1;
        }
        return *this;
    }

    /// @brief Open UDP socket and bind to a local port and IP address.
    /// @param port Local port to bind to (0 for ephemeral).
    /// @param ip Local interface IP address (default: "0.0.0.0").
    /// @return true if socket was created and bound successfully.
    bool openAndBind(uint16_t port = 0, const char* ip = "0.0.0.0") noexcept {
#if CORIUM_HAS_UDP_SOCKETS
        close();

#if defined(_WIN32) || defined(_WIN64)
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) {
            return false;
        }
        m_fd = static_cast<int>(sock);
#else
        m_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (m_fd < 0) {
            return false;
        }
#endif

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
#if defined(_WIN32) || defined(_WIN64)
        InetPtonA(AF_INET, ip, &addr.sin_addr);
#else
        inet_pton(AF_INET, ip, &addr.sin_addr);
#endif

        if (::bind(m_fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
            close();
            return false;
        }
        return true;
#else
        (void)port;
        (void)ip;
        return false;
#endif
    }

    /// @brief Set socket non-blocking mode.
    /// @param nonBlocking true for non-blocking I/O.
    /// @return true on success.
    bool setNonBlocking(bool nonBlocking = true) noexcept {
#if CORIUM_HAS_UDP_SOCKETS
        if (m_fd < 0) {
            return false;
        }
#if defined(_WIN32) || defined(_WIN64)
        u_long mode = nonBlocking ? 1 : 0;
        return ioctlsocket(static_cast<SOCKET>(m_fd), FIONBIO, &mode) == 0;
#else
        int flags = fcntl(m_fd, F_GETFL, 0);
        if (flags < 0) return false;
        flags = nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
        return fcntl(m_fd, F_SETFL, flags) == 0;
#endif
#else
        (void)nonBlocking;
        return false;
#endif
    }

    /// @brief Send raw payload to target UDP endpoint.
    /// @param ip Target IPv4 address string (e.g. "127.0.0.1").
    /// @param port Target port.
    /// @param data Byte payload to transmit.
    /// @return true if datagram was sent successfully.
    bool sendTo(const char* ip, uint16_t port, std::span<const uint8_t> data) noexcept {
#if CORIUM_HAS_UDP_SOCKETS
        if (m_fd < 0) {
            // Lazy socket creation if not bound
            if (!openAndBind(0, "0.0.0.0")) {
                return false;
            }
        }

        sockaddr_in destAddr{};
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(port);
#if defined(_WIN32) || defined(_WIN64)
        InetPtonA(AF_INET, ip, &destAddr.sin_addr);
        int sent = ::sendto(
            static_cast<SOCKET>(m_fd),
            reinterpret_cast<const char*>(data.data()),
            static_cast<int>(data.size()),
            0,
            reinterpret_cast<const sockaddr*>(&destAddr),
            sizeof(destAddr)
        );
#else
        inet_pton(AF_INET, ip, &destAddr.sin_addr);
        ssize_t sent = ::sendto(
            m_fd,
            data.data(),
            data.size(),
            0,
            reinterpret_cast<const sockaddr*>(&destAddr),
            sizeof(destAddr)
        );
#endif
        return sent == static_cast<ssize_t>(data.size());
#else
        (void)ip;
        (void)port;
        (void)data;
        return false;
#endif
    }

    /// @brief Serialize and send a typed event over UDP using Corium WirePacket framing.
    /// @tparam Event Concrete event type (must be trivially copyable).
    /// @tparam EventVariant Variant list for type indexing.
    /// @param ip Target IPv4 address.
    /// @param port Target port.
    /// @param event Event instance to transmit.
    /// @return true if packet was serialized and sent.
    template <typename Event, typename EventVariant>
    bool sendEvent(const char* ip, uint16_t port, const Event& event) noexcept {
        auto packet = corium::wire::WireSerializer::serialize<Event, EventVariant, MaxPacketSize>(event);
        return sendTo(ip, port, std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(&packet), packet.totalWireSize()));
    }

    /// @brief Receive raw bytes from incoming UDP datagram.
    /// @param bufferOut Output buffer.
    /// @param bytesReceivedOut Number of bytes received.
    /// @return true if datagram was received.
    bool receive(std::span<uint8_t> bufferOut, size_t& bytesReceivedOut) noexcept {
#if CORIUM_HAS_UDP_SOCKETS
        if (m_fd < 0) {
            bytesReceivedOut = 0;
            return false;
        }

#if defined(_WIN32) || defined(_WIN64)
        int recvd = ::recvfrom(
            static_cast<SOCKET>(m_fd),
            reinterpret_cast<char*>(bufferOut.data()),
            static_cast<int>(bufferOut.size()),
            0, nullptr, nullptr
        );
#else
        ssize_t recvd = ::recvfrom(
            m_fd,
            bufferOut.data(),
            bufferOut.size(),
            0, nullptr, nullptr
        );
#endif
        if (recvd <= 0) {
            bytesReceivedOut = 0;
            return false;
        }
        bytesReceivedOut = static_cast<size_t>(recvd);
        return true;
#else
        (void)bufferOut;
        bytesReceivedOut = 0;
        return false;
#endif
    }

    /// @brief Receive a WirePacket and deserialize directly into a Corium EventSink.
    /// @tparam EventVariant Variant list of all supported events.
    /// @tparam Sink Target EventSink or EventBus.
    /// @param sink Target sink.
    /// @param priority Priority to assign to the deserialized event.
    /// @return true if a valid event packet was received and pushed into sink.
    template <typename EventVariant, typename Sink>
    bool receiveAndPush(Sink& sink, EventPriority priority = EventPriority::Normal) noexcept {
        size_t recvd = 0;
        if (!receive(std::span<uint8_t>(m_rxBuffer.data(), m_rxBuffer.size()), recvd)) {
            return false;
        }

        if (recvd < sizeof(corium::wire::WireHeader)) {
            return false;
        }

        corium::wire::WirePacket<MaxPacketSize> packet{};
        std::memcpy(&packet, m_rxBuffer.data(), recvd > sizeof(packet) ? sizeof(packet) : recvd);
        return corium::wire::WireSerializer::deserializeAndPush<EventVariant, MaxPacketSize, Sink>(
            packet, sink, priority);
    }

    /// @brief Close underlying socket.
    void close() noexcept {
#if CORIUM_HAS_UDP_SOCKETS
        if (m_fd >= 0) {
#if defined(_WIN32) || defined(_WIN64)
            closesocket(static_cast<SOCKET>(m_fd));
#else
            ::close(m_fd);
#endif
            m_fd = -1;
        }
#endif
    }

    /// @brief Returns true if socket is open and bound.
    [[nodiscard]] bool isOpen() const noexcept {
        return m_fd >= 0;
    }

    /// @brief Native socket file descriptor or handle.
    [[nodiscard]] int nativeHandle() const noexcept {
        return m_fd;
    }

private:
    int m_fd{-1};
    std::array<uint8_t, sizeof(corium::wire::WireHeader) + MaxPacketSize> m_rxBuffer{};
};

} // namespace corium::net

// <<< End: corium/net/StaticUdpChannel.hpp

// <<< End: corium/net/net.hpp

// >>> Begin: corium/EventRouter.hpp
/**
 * @file EventRouter.hpp
 * @ingroup core
 * @brief Zero-heap topic-based multi-subscriber event routing and fan-out dispatcher.
 */


#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <variant>


namespace corium {

/// @ingroup core
/// @brief Zero-heap static topic-based publish/subscribe router.
/// Fans out events to multiple registered delegate subscribers per topic ID without dynamic memory allocation.
/// @tparam EventVariant Variant containing all supported event types.
/// @tparam MaxSubscribersPerTopic Maximum subscribers registered per topic (default: 8).
/// @tparam MaxTopics Maximum distinct topics supported (default: 8).
template <
    typename EventVariant,
    size_t MaxSubscribersPerTopic = 8,
    size_t MaxTopics = 8
>
class EventRouter {
public:
    using DelegateType = internal::EventHandlerDelegate<EventVariant, 32>;

    constexpr EventRouter() noexcept = default;

    /// @brief Subscribe a delegate callback to a specific topic ID.
    /// @param topicId Topic identifier.
    /// @param subscriber Delegate callback to execute when an event is published to this topic.
    /// @return true if subscriber was registered; false if topic or subscriber slots are full.
    bool subscribe(uint32_t topicId, DelegateType subscriber) noexcept {
        // Find existing topic slot
        for (size_t i = 0; i < m_numTopics; ++i) {
            if (m_topics[i].topicId == topicId) {
                if (m_topics[i].subscriberCount >= MaxSubscribersPerTopic) {
                    return false; // Topic subscriber capacity reached
                }
                m_topics[i].subscribers[m_topics[i].subscriberCount++] = std::move(subscriber);
                return true;
            }
        }

        // Allocate new topic slot
        if (m_numTopics >= MaxTopics) {
            return false; // Max topics reached
        }

        auto& newTopic = m_topics[m_numTopics++];
        newTopic.topicId = topicId;
        newTopic.subscriberCount = 1;
        newTopic.subscribers[0] = std::move(subscriber);
        return true;
    }

    /// @brief Subscribe a typed event handler lambda to a specific topic ID.
    /// @tparam Event Concrete event type to filter on.
    /// @tparam Callable Lambda/Functor accepting `const Event&`.
    /// @param topicId Topic identifier.
    /// @param callable Handler function.
    /// @return true if subscribed successfully.
    template <typename Event, typename Callable>
    bool subscribeEvent(uint32_t topicId, Callable callable) noexcept {
        return subscribe(topicId, DelegateType([c = std::move(callable)](const EventVariant& var) {
            if (std::holds_alternative<Event>(var)) {
                c(std::get<Event>(var));
            }
        }));
    }

    /// @brief Publish an event variant to all subscribers of a specific topic.
    /// @param topicId Target topic ID.
    /// @param event Event instance to dispatch.
    /// @return Number of subscribers invoked.
    size_t publish(uint32_t topicId, const EventVariant& event) const noexcept {
        for (size_t i = 0; i < m_numTopics; ++i) {
            if (m_topics[i].topicId == topicId) {
                for (size_t j = 0; j < m_topics[i].subscriberCount; ++j) {
                    m_topics[i].subscribers[j](event);
                }
                return m_topics[i].subscriberCount;
            }
        }
        return 0;
    }

    /// @brief Publish a concrete event to all subscribers of a specific topic.
    /// @tparam Event Concrete event type.
    /// @param topicId Target topic ID.
    /// @param event Event instance to dispatch.
    /// @return Number of subscribers invoked.
    template <typename Event>
    size_t publishEvent(uint32_t topicId, const Event& event) const noexcept {
        return publish(topicId, EventVariant{event});
    }

    /// @brief Reset all topic subscriptions.
    void clear() noexcept {
        m_numTopics = 0;
        for (auto& topic : m_topics) {
            topic.subscriberCount = 0;
        }
    }

    /// @brief Total active topics currently configured.
    [[nodiscard]] size_t topicCount() const noexcept {
        return m_numTopics;
    }

private:
    struct TopicSlot {
        uint32_t topicId{0};
        size_t subscriberCount{0};
        std::array<DelegateType, MaxSubscribersPerTopic> subscribers{};
    };

    std::array<TopicSlot, MaxTopics> m_topics{};
    size_t m_numTopics{0};
};

} // namespace corium

// <<< End: corium/EventRouter.hpp
// IWYU pragma: end_exports

// <<< End: corium/corium.hpp
