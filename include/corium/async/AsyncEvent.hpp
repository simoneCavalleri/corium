/**
 * @file AsyncEvent.hpp
 * @ingroup async
 * @brief Lock-free asynchronous event synchronization primitive for C++20 coroutines.
 */

#pragma once

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
