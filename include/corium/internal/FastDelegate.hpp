#pragma once

#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace corium {

/// @brief Lightweight non-allocating delegate wrapper with 32-byte Small Buffer Optimization (SBO).
/// Eliminates std::function heap allocations and virtual table overhead for event handlers.
/// @tparam EventType The concrete event type invoked by this delegate.
template <typename EventType>
class EventHandlerDelegate {
    static constexpr std::size_t InlineSize = 32;

    using StubFn = void (*)(void* instance, const EventType& event);
    using DestroyFn = void (*)(void* instance) noexcept;
    using MoveFn = void (*)(void* destStorage, void*& destInstance, void*& srcInstance) noexcept;

public:
    EventHandlerDelegate() noexcept = default;

    /// @brief Construct delegate wrapping a callable object (lambda, function pointer, or functor).
    /// @tparam Handler Callable type.
    /// @param handler Callable instance.
    template <
        typename Handler,
        typename Decayed = std::decay_t<Handler>,
        typename = std::enable_if_t<
            !std::is_same_v<Decayed, EventHandlerDelegate> &&
            std::is_invocable_r_v<void, Decayed&, const EventType&>
        >
    >
    explicit EventHandlerDelegate(Handler&& handler)
    {
        static_assert(
            std::is_nothrow_destructible_v<Decayed>,
            "Handler destructor must be noexcept"
        );

        constexpr bool fitsInline =
            sizeof(Decayed) <= InlineSize &&
            alignof(Decayed) <= alignof(std::max_align_t) &&
            std::is_nothrow_move_constructible_v<Decayed>;

        if constexpr (fitsInline) {
            void* storage = static_cast<void*>(_inlineStorage);

            ::new (storage) Decayed(std::forward<Handler>(handler));

            _instance = storage;

            _destroy = [](void* instance) noexcept {
                reinterpret_cast<Decayed*>(instance)->~Decayed();
            };

            _move = [](void* destStorage, void*& destInstance, void*& srcInstance) noexcept {
                auto* src = reinterpret_cast<Decayed*>(srcInstance);

                ::new (destStorage) Decayed(std::move(*src));
                src->~Decayed();

                destInstance = destStorage;
                srcInstance = nullptr;
            };
        } else {
            auto* heapObj = new Decayed(std::forward<Handler>(handler));

            _instance = heapObj;

            _destroy = [](void* instance) noexcept {
                delete reinterpret_cast<Decayed*>(instance);
            };

            _move = [](void*, void*& destInstance, void*& srcInstance) noexcept {
                destInstance = srcInstance;
                srcInstance = nullptr;
            };
        }

        _stub = [](void* instance, const EventType& event) {
            (*reinterpret_cast<Decayed*>(instance))(event);
        };
    }

    ~EventHandlerDelegate()
    {
        reset();
    }

    EventHandlerDelegate(EventHandlerDelegate&& rhs) noexcept
    {
        moveFrom(std::move(rhs));
    }

    EventHandlerDelegate& operator=(EventHandlerDelegate&& rhs) noexcept
    {
        if (this != &rhs) {
            reset();
            moveFrom(std::move(rhs));
        }

        return *this;
    }

    EventHandlerDelegate(const EventHandlerDelegate&) = delete;
    EventHandlerDelegate& operator=(const EventHandlerDelegate&) = delete;

    /// @brief Invoke wrapped handler directly with concrete event.
    /// @param event Event instance to pass to handler.
    void invoke(const EventType& event) const
    {
        assert(_stub && "Invoking an empty EventHandlerDelegate");

        if (!_stub) {
            return;
        }

        _stub(_instance, event);
    }

    /// @brief Check if delegate wraps a valid handler.
    explicit operator bool() const noexcept
    {
        return _stub != nullptr;
    }

    /// @brief Reset delegate to empty state.
    void reset() noexcept
    {
        if (_destroy) {
            _destroy(_instance);
        }

        _instance = nullptr;
        _stub = nullptr;
        _destroy = nullptr;
        _move = nullptr;
    }

private:
    void moveFrom(EventHandlerDelegate&& rhs) noexcept
    {
        _stub = rhs._stub;
        _destroy = rhs._destroy;
        _move = rhs._move;

        if (_move) {
            _move(
                static_cast<void*>(_inlineStorage),
                _instance,
                rhs._instance
            );
        } else {
            _instance = nullptr;
        }

        rhs._stub = nullptr;
        rhs._destroy = nullptr;
        rhs._move = nullptr;
    }

private:
    alignas(std::max_align_t) std::byte _inlineStorage[InlineSize]{};

    void* _instance = nullptr;
    StubFn _stub = nullptr;
    DestroyFn _destroy = nullptr;
    MoveFn _move = nullptr;
};

} // namespace corium