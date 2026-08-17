#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>

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
        : fn(reinterpret_cast<ContextFn>(simpleFn)), arg(nullptr)
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
        std::lock_guard<std::mutex> lock(_mutex);
        _callback = callback;
    }

    void signal()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _hasEvents = true;
        _cv.notify_one();
        if (_callback) {
            _callback();
        }
    }

    template <typename Rep, typename Period>
    void wait_for(const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _cv.wait_for(lock, timeout, [this]() { return _hasEvents; });
        _hasEvents = false;
    }

private:
    StaticCallback _callback;
    std::mutex _mutex;
    std::condition_variable _cv;
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
        _flag.notify_one();
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
            std::this_thread::yield();
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
