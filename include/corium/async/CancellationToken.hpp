#pragma once

#include <atomic>
#include <coroutine>

namespace corium::async {

/// @ingroup async
/// @brief Lightweight, zero-heap cooperative cancellation token for C++20 coroutines and services.
class CancellationToken {
public:
    constexpr CancellationToken() noexcept = default;

    /// @brief Signal cancellation to all observing tasks.
    void cancel() noexcept
    {
        _cancelled.store(true, std::memory_order_release);
        auto handle = _waiter.exchange(nullptr, std::memory_order_acq_rel);
        if (handle && !handle.done()) {
            handle.resume();
        }
    }

    /// @brief Check if cancellation has been requested.
    [[nodiscard]] bool isCancelled() const noexcept
    {
        return _cancelled.load(std::memory_order_acquire);
    }

    /// @brief Reset token state to uncancelled.
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
    [[nodiscard]] WhenCancelledAwaiter whenCancelled() noexcept
    {
        return WhenCancelledAwaiter{*this};
    }

private:
    std::atomic<bool> _cancelled{false};
    std::atomic<std::coroutine_handle<>> _waiter{nullptr};
};

} // namespace corium::async
