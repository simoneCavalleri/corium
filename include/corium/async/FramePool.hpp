/**
 * @file FramePool.hpp
 * @ingroup async
 * @brief Zero-heap static coroutine frame pool and frame allocator policies.
 */

#pragma once

#include <array>
#include <cstddef>
#include <new>

namespace corium::async {

/// @brief Default heap allocator policy for C++20 coroutine frames (HALO-eligible).
struct HeapFrameAllocator {
    [[nodiscard]] static void* allocate(std::size_t size) {
        return ::operator new(size);
    }

    static void deallocate(void* ptr, std::size_t size) noexcept {
        ::operator delete(ptr, size);
    }
};

/// @ingroup async
/// @brief Statically-allocated fixed-capacity memory pool for coroutine state frames.
/// Eliminates heap allocations and guarantees deterministic O(1) coroutine frame allocation.
/// @tparam MaxFrames Maximum number of concurrent active coroutine frames.
/// @tparam FrameSize Maximum size in bytes reserved for each coroutine frame.
template <std::size_t MaxFrames = 16, std::size_t FrameSize = 256>
class StaticFramePool {
public:
    static constexpr std::size_t max_frames = MaxFrames;
    static constexpr std::size_t frame_size = FrameSize;

    struct alignas(std::max_align_t) Slot {
        std::byte storage[FrameSize];
        bool inUse{false};
    };

    [[nodiscard]] static void* allocate(std::size_t size) noexcept {
        if (size > FrameSize) {
            return nullptr;
        }

        for (auto& slot : _slots) {
            if (!slot.inUse) {
                slot.inUse = true;
                return static_cast<void*>(slot.storage);
            }
        }
        return nullptr; // Pool exhausted
    }

    static void deallocate(void* ptr, std::size_t /*size*/) noexcept {
        if (!ptr) {
            return;
        }

        for (auto& slot : _slots) {
            if (static_cast<void*>(slot.storage) == ptr) {
                slot.inUse = false;
                return;
            }
        }
    }

    /// @brief Number of active frames currently allocated from the pool.
    [[nodiscard]] static std::size_t activeCount() noexcept {
        std::size_t count = 0;
        for (const auto& slot : _slots) {
            if (slot.inUse) {
                count++;
            }
        }
        return count;
    }

    /// @brief Reset all pool slots to available state.
    static void reset() noexcept {
        for (auto& slot : _slots) {
            slot.inUse = false;
        }
    }

private:
    static inline std::array<Slot, MaxFrames> _slots{};
};

/// @brief Allocator policy adapter wrapping a StaticFramePool.
template <std::size_t MaxFrames = 16, std::size_t FrameSize = 256>
struct StaticFrameAllocator {
    using Pool = StaticFramePool<MaxFrames, FrameSize>;

    [[nodiscard]] static void* allocate(std::size_t size) {
        void* ptr = Pool::allocate(size);
        if (!ptr) {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
            throw std::bad_alloc();
#else
            return nullptr;
#endif
        }
        return ptr;
    }

    static void deallocate(void* ptr, std::size_t size) noexcept {
        Pool::deallocate(ptr, size);
    }
};

} // namespace corium::async
