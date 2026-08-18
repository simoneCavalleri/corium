/**
 * @file Channel.hpp
 * @ingroup async
 * @brief Zero-heap bounded asynchronous channel for C++20 coroutine message passing.
 */

#pragma once

#include <array>
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <optional>
#include <type_traits>
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
