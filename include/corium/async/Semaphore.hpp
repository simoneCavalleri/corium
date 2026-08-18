/**
 * @file Semaphore.hpp
 * @ingroup async
 * @brief Zero-heap asynchronous counting semaphore for C++20 coroutines.
 */

#pragma once

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
