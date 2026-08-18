#pragma once

#include <coroutine>
#include <exception>
#include <iterator>
#include <utility>

namespace corium::async {

/// @ingroup async
/// @brief Zero-heap C++20 pull-based lazy generator sequence.
/// Compatible with range-based for loops and standard C++20 ranges.
/// @tparam T Value type yielded by the generator.
template <typename T>
class Generator {
public:
    struct promise_type {
        const T* currentValue{nullptr};
        std::exception_ptr exception{nullptr};

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

} // namespace corium::async
