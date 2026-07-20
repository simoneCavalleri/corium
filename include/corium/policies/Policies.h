#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

#include "corium/Events.h"
#include "corium/internal/MpscRingBuffer.h"
#include "corium/internal/Reactor.h"

#ifdef __linux__
#include <sys/eventfd.h>
#include <unistd.h>
#endif

namespace corium {

// =============================================================================
// 1. Queue Policies
// =============================================================================

template <typename EventVariant = DefaultEvents, size_t Capacity = 1024>
class BoundedMpscQueuePolicy {
public:
    using EventType = EventVariant;

    struct PushResult {
        bool pushed;
        bool wasEmpty;
    };

    PushResult tryPush(EventVariant event)
    {
        auto res = _ringBuffer.tryPush(std::move(event));
        return {res.pushed, res.wasEmpty};
    }

    bool tryPop(EventVariant& event)
    {
        return _ringBuffer.tryPop(event);
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return _ringBuffer.empty();
    }

private:
    MpscRingBuffer<EventVariant, Capacity> _ringBuffer;
};

template <typename EventVariant = DefaultEvents>
class BlockingQueuePolicy {
public:
    using EventType = EventVariant;

    struct PushResult {
        bool pushed;
        bool wasEmpty;
    };

    PushResult tryPush(EventVariant event)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        bool wasEmpty = _queue.empty();
        _queue.push(std::move(event));
        return {true, wasEmpty};
    }

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

class CallbackSignalPolicy {
public:
    void setOnEventsAvailable(std::function<void()> callback)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _callback = std::move(callback);
    }

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

class AtomicWaitSignalPolicy {
public:
    void setOnEventsAvailable(std::function<void()> callback)
    {
        _userCallback = std::move(callback);
    }

    void signal()
    {
        _counter.fetch_add(1, std::memory_order_release);
        _counter.notify_one();
        if (_userCallback) {
            _userCallback();
        }
    }

    void wait()
    {
        uint32_t current = _counter.load(std::memory_order_acquire);
        _counter.wait(current);
    }

private:
    std::atomic<uint32_t> _counter{0};
    std::function<void()> _userCallback;
};

class NoSignalPolicy {
public:
    void setOnEventsAvailable(std::function<void()>) {}
    void signal() noexcept {}
};

#ifdef __linux__
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

    void setOnEventsAvailable(std::function<void()> callback)
    {
        _userCallback = std::move(callback);
    }

    void signal()
    {
        uint64_t val = 1;
        [[maybe_unused]] auto res = ::write(_fd, &val, sizeof(val));
        if (_userCallback) {
            _userCallback();
        }
    }

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

template <typename EventVariant = DefaultEvents>
class StaticReactorPolicy {
public:
    using EventType = EventVariant;

    template <typename EventTypeSingle, typename Handler>
    void registerHandler(Handler&& handler)
    {
        _reactor.template registerHandler<EventTypeSingle>(std::forward<Handler>(handler));
    }

    void dispatch(const EventVariant& event)
    {
        _reactor.dispatch(event);
    }

    void seal()
    {
        _reactor.seal();
    }

private:
    ReactorT<EventVariant> _reactor;
};

} // namespace corium
