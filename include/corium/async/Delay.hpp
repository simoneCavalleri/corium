#pragma once

#include <chrono>
#include <coroutine>
#include <thread>

namespace corium::async {

/// @brief Awaitable that yields control back to the caller/event loop once.
struct YieldAwaiter {
    constexpr bool await_ready() const noexcept { return false; }
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

    constexpr bool await_ready() const noexcept {
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
