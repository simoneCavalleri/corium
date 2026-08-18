/**
 * @file StaticMinHeap.hpp
 * @ingroup internal
 * @brief Fixed-capacity zero-heap binary min-heap for deterministic priority queues and timers.
 */

#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <functional>
#include <utility>

namespace corium::internal {

/// @brief Fixed-capacity statically-allocated binary min-heap.
/// Zero dynamic heap allocations.
/// @tparam T Element type stored in the heap.
/// @tparam Capacity Maximum number of elements.
/// @tparam Compare Comparator predicate (std::greater<T> for min-heap where smallest element is at top).
template <
    typename T,
    std::size_t Capacity,
    typename Compare = std::greater<T>
>
class StaticMinHeap {
public:
    constexpr StaticMinHeap() = default;

    /// @brief Push an element into the heap.
    /// @return true if pushed, false if capacity exceeded.
    template <typename U>
    bool push(U&& value) {
        if (_size >= Capacity) {
            return false;
        }

        _data[_size] = std::forward<U>(value);
        siftUp(_size);
        _size++;
        return true;
    }

    /// @brief Remove the top element from the heap.
    /// @return true if popped, false if heap was empty.
    bool pop() noexcept {
        if (_size == 0) {
            return false;
        }

        _size--;
        if (_size > 0) {
            _data[0] = std::move(_data[_size]);
            siftDown(0);
        }
        return true;
    }

    /// @brief Access top element.
    [[nodiscard]] const T& top() const noexcept {
        assert(_size > 0 && "Accessing top of empty StaticMinHeap");
        return _data[0];
    }

    /// @brief Access mutable top element.
    [[nodiscard]] T& top() noexcept {
        assert(_size > 0 && "Accessing top of empty StaticMinHeap");
        return _data[0];
    }

    /// @brief Current number of elements in the heap.
    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return _size;
    }

    /// @brief Maximum capacity of the heap.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

    /// @brief Check if heap is empty.
    [[nodiscard]] constexpr bool empty() const noexcept {
        return _size == 0;
    }

    /// @brief Check if heap is full.
    [[nodiscard]] constexpr bool full() const noexcept {
        return _size >= Capacity;
    }

    /// @brief Clear all elements.
    void clear() noexcept {
        _size = 0;
    }

    /// @brief Direct access to underlying data array.
    [[nodiscard]] T* data() noexcept {
        return _data.data();
    }

    /// @brief Direct const access to underlying data array.
    [[nodiscard]] const T* data() const noexcept {
        return _data.data();
    }

    /// @brief Sift down an element at specified index (e.g. after in-place modification).
    void siftDown(std::size_t index) noexcept {
        Compare comp;
        std::size_t current = index;

        while (true) {
            std::size_t left = 2 * current + 1;
            std::size_t right = 2 * current + 2;
            std::size_t smallest = current;

            if (left < _size && comp(_data[smallest], _data[left])) {
                smallest = left;
            }
            if (right < _size && comp(_data[smallest], _data[right])) {
                smallest = right;
            }

            if (smallest != current) {
                using std::swap;
                swap(_data[current], _data[smallest]);
                current = smallest;
            } else {
                break;
            }
        }
    }

    /// @brief Sift up an element at specified index.
    void siftUp(std::size_t index) noexcept {
        Compare comp;
        std::size_t current = index;

        while (current > 0) {
            std::size_t parent = (current - 1) / 2;
            if (comp(_data[parent], _data[current])) {
                using std::swap;
                swap(_data[parent], _data[current]);
                current = parent;
            } else {
                break;
            }
        }
    }

private:
    std::array<T, Capacity> _data{};
    std::size_t _size{0};
};

} // namespace corium::internal
