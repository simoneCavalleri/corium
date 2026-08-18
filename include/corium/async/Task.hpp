/**
 * @file Task.hpp
 * @ingroup async
 * @brief Lazy awaitable C++20 coroutine task with zero dynamic heap allocation.
 */

#pragma once

#include <coroutine>
#include <exception>
#include <utility>

#include "corium/async/FramePool.hpp"

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

        static void operator delete(void* ptr) noexcept {
            Allocator::deallocate(ptr, 0);
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

        static void operator delete(void* ptr) noexcept {
            Allocator::deallocate(ptr, 0);
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
