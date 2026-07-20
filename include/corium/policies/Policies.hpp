#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

#include "corium/Events.hpp"
#include "corium/internal/MpscRingBuffer.hpp"
#include "corium/internal/Reactor.hpp"

#ifdef __linux__
#include <sys/eventfd.h>
#include <unistd.h>
#endif

namespace corium {

// =============================================================================
// 1. Queue Policies
// =============================================================================

/// @brief Queue Policy for fixed-capacity, zero-allocation lock-free MPSC RingBuffer.
/// @tparam EventVariant The variant type list of supported events.
/// @tparam Capacity Ring buffer capacity (must be a power of 2).
template <typename EventVariant = DefaultEvents, size_t Capacity = 1024>
class BoundedMpscQueuePolicy {
public:
    using EventType = EventVariant;

    struct PushResult {
        bool pushed;
        bool wasEmpty;
    };

    /// @brief Try to push an event into the lock-free queue (thread-safe, zero allocations).
    PushResult tryPush(EventVariant event)
    {
        auto res = _ringBuffer.tryPush(std::move(event));
        return {res.pushed, res.wasEmpty};
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
    PushResult tryPush(EventVariant event)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        bool wasEmpty = _queue.empty();
        _queue.push(std::move(event));
        return {true, wasEmpty};
    }

    /// @brief Try to pop an event from the blocking queue.
    bool tryPop(EventVariant& event)
    {
        std::lock_guard<std::mutex> lock(_mutex);
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
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.empty();
    }

private:
    std::queue<EventVariant> _queue;
    mutable std::mutex _mutex;
};

// =============================================================================
// 2. Signal Policies
// =============================================================================

/// @brief Signal Policy invoking an edge-triggered callback on 0->1 transition when queue becomes non-empty.
class CallbackSignalPolicy {
public:
    /// @brief Set callback triggered when queue becomes non-empty.
    void setOnQueueNonEmpty(std::function<void()> callback)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _callback = std::move(callback);
    }

    /// @brief Signal that events are available.
    void signal()
    {
        std::function<void()> cb;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            cb = _callback;
        }
        if (cb) {
            cb();
        }
    }

private:
    std::function<void()> _callback;
    std::mutex _mutex;
};

/// @brief Signal Policy using C++20 std::atomic::wait() / notify_one() for zero-mutex futex signaling.
class AtomicWaitSignalPolicy {
public:
    /// @brief Set callback triggered when queue becomes non-empty.
    void setOnQueueNonEmpty(std::function<void()> callback)
    {
        _userCallback = std::move(callback);
    }

    /// @brief Signal counter increase and notify waiting thread via std::atomic::notify_one().
    void signal()
    {
        _counter.fetch_add(1, std::memory_order_release);
        _counter.notify_one();
        if (_userCallback) {
            _userCallback();
        }
    }

    /// @brief Wait until counter value changes (C++20 std::atomic::wait).
    void wait()
    {
        uint32_t current = _counter.load(std::memory_order_acquire);
        _counter.wait(current);
    }

private:
    std::atomic<uint32_t> _counter{0};
    std::function<void()> _userCallback;
};

/// @brief Signal Policy for busy-spin / polling event loops (sub-microsecond latency, zero signaling cost).
class NoSignalPolicy {
public:
    void setOnQueueNonEmpty(std::function<void()>) {}
    void signal() noexcept {}
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

    void setOnQueueNonEmpty(std::function<void()> callback)
    {
        _userCallback = std::move(callback);
    }

    /// @brief Signal by writing a 64-bit counter to the Linux eventfd descriptor.
    void signal()
    {
        uint64_t val = 1;
        [[maybe_unused]] auto res = ::write(_fd, &val, sizeof(val));
        if (_userCallback) {
            _userCallback();
        }
    }

    /// @brief Access native Linux eventfd file descriptor for epoll integration.
    [[nodiscard]] int nativeHandle() const noexcept
    {
        return _fd;
    }

private:
    int _fd = -1;
    std::function<void()> _userCallback;
};
#endif

// =============================================================================
// 3. Dispatch Policies
// =============================================================================

/// @brief Dispatch Policy wrapping Static Reactor with direct array lookup and zero type erasure.
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
class StaticReactorPolicy {
public:
    using EventType = EventVariant;

    /// @brief Register handler for concrete event type.
    template <typename EventTypeSingle, typename Handler>
    void registerHandler(Handler&& handler)
    {
        _reactor.template registerHandler<EventTypeSingle>(std::forward<Handler>(handler));
    }

    /// @brief Dispatch event via static array indexing and FastDelegate.
    void dispatch(const EventVariant& event)
    {
        _reactor.dispatch(event);
    }

    /// @brief Seal reactor handlers.
    void seal()
    {
        _reactor.seal();
    }

private:
    ReactorT<EventVariant> _reactor;
};

} // namespace corium
