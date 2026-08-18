/**
 * @file ShmMpscQueue.hpp
 * @ingroup ipc
 * @brief Lock-free multi-producer single-consumer queue located in shared memory.
 */

#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace corium::ipc {

constexpr uint32_t CORIUM_SHM_MAGIC = 0x434F5249; // "CORI"
constexpr uint32_t CORIUM_SHM_VERSION = 1;

/// @brief Lock-free, zero-allocation multi-producer single-consumer ring buffer layout for shared memory.
/// Compatible with POD and trivially copyable types (or serialized payloads).
/// @tparam T Value type stored in each ring cell.
/// @tparam Capacity Number of slots (must be a power of 2).
template <typename T, std::size_t Capacity = 256>
class ShmMpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert(std::is_trivially_copyable_v<T>, "ShmMpscQueue value type T must be trivially copyable for shared memory safety.");

public:
    static constexpr std::size_t BufferCapacity = Capacity;
    static constexpr std::size_t BufferMask = Capacity - 1;

    struct Cell {
        alignas(64) std::atomic<std::size_t> sequence{0};
        alignas(alignof(T)) uint8_t storage[sizeof(T)]{};

        [[nodiscard]] T* ptr() noexcept
        {
            return reinterpret_cast<T*>(storage);
        }

        [[nodiscard]] const T* ptr() const noexcept
        {
            return reinterpret_cast<const T*>(storage);
        }
    };

    struct Layout {
        uint32_t magic{CORIUM_SHM_MAGIC};
        uint32_t version{CORIUM_SHM_VERSION};
        uint32_t capacity{Capacity};
        uint32_t elementSize{sizeof(T)};

        alignas(64) std::atomic<std::size_t> enqueuePos{0};
        alignas(64) std::atomic<std::size_t> dequeuePos{0};

        Cell cells[Capacity];
    };

    ShmMpscQueue() = default;

    /// @brief Construct queue bound to a mapped shared memory address.
    explicit ShmMpscQueue(void* mappedAddress, bool initializeMemory = false) noexcept
    {
        bind(mappedAddress, initializeMemory);
    }

    /// @brief Bind to a mapped shared memory region.
    /// @param mappedAddress Pointer to shared memory.
    /// @param initializeMemory If true, resets header and cell sequences (creator process).
    void bind(void* mappedAddress, bool initializeMemory = false) noexcept
    {
        _layout = static_cast<Layout*>(mappedAddress);
        if (_layout && initializeMemory) {
            _layout->magic = CORIUM_SHM_MAGIC;
            _layout->version = CORIUM_SHM_VERSION;
            _layout->capacity = Capacity;
            _layout->elementSize = sizeof(T);
            _layout->enqueuePos.store(0, std::memory_order_relaxed);
            _layout->dequeuePos.store(0, std::memory_order_relaxed);

            for (std::size_t i = 0; i < Capacity; ++i) {
                _layout->cells[i].sequence.store(i, std::memory_order_relaxed);
            }
        }
    }

    /// @brief Required byte size of the shared memory layout.
    [[nodiscard]] static constexpr std::size_t requiredMemorySize() noexcept
    {
        return sizeof(Layout);
    }

    /// @brief Validate that the mapped shared memory contains a compatible ShmMpscQueue header.
    [[nodiscard]] bool isValid() const noexcept
    {
        if (!_layout) {
            return false;
        }
        return _layout->magic == CORIUM_SHM_MAGIC &&
               _layout->version == CORIUM_SHM_VERSION &&
               _layout->capacity == Capacity &&
               _layout->elementSize == sizeof(T);
    }

    /// @brief Lock-free push into shared memory queue (multi-producer safe).
    /// @param item Value to push.
    /// @return true if pushed, false if queue is full.
    template <typename U>
    bool tryPush(U&& item) noexcept
    {
        if (!_layout) {
            return false;
        }

        Cell* cell = nullptr;
        std::size_t pos = _layout->enqueuePos.load(std::memory_order_relaxed);

        for (;;) {
            cell = &_layout->cells[pos & BufferMask];
            const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (dif == 0) {
                if (_layout->enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (dif < 0) {
                // Buffer is full
                return false;
            } else {
                pos = _layout->enqueuePos.load(std::memory_order_relaxed);
            }
        }

        // Construct / copy item into cell storage
        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memcpy(cell->storage, &item, sizeof(T));
        } else {
            new (cell->storage) T(std::forward<U>(item));
        }

        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    /// @brief Lock-free pop from shared memory queue (single-consumer safe).
    /// @param outItem Reference populated with popped value.
    /// @return true if popped, false if queue is empty.
    bool tryPop(T& outItem) noexcept
    {
        if (!_layout) {
            return false;
        }

        std::size_t pos = _layout->dequeuePos.load(std::memory_order_relaxed);
        Cell* cell = &_layout->cells[pos & BufferMask];
        const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
        const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

        if (dif < 0) {
            // Buffer is empty
            return false;
        }

        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memcpy(&outItem, cell->storage, sizeof(T));
        } else {
            outItem = std::move(*cell->ptr());
            cell->ptr()->~T();
        }

        cell->sequence.store(pos + Capacity, std::memory_order_release);
        _layout->dequeuePos.store(pos + 1, std::memory_order_relaxed);
        return true;
    }

    /// @brief Check if queue is currently empty.
    [[nodiscard]] bool empty() const noexcept
    {
        if (!_layout) return true;
        const std::size_t pos = _layout->dequeuePos.load(std::memory_order_relaxed);
        const Cell* cell = &_layout->cells[pos & BufferMask];
        const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
        return static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1) < 0;
    }

    /// @brief Get configured capacity of the queue.
    [[nodiscard]] constexpr std::size_t capacity() const noexcept
    {
        return Capacity;
    }

private:
    Layout* _layout{nullptr};
};

} // namespace corium::ipc
