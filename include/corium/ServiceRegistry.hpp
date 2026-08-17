#pragma once

#include <array>
#include <cstddef>
#include <stop_token>
#include <type_traits>
#include <utility>

#include "corium/BackgroundService.hpp"
#include "corium/ServiceContext.hpp"

namespace corium {

namespace internal {

template <typename T, typename Context>
concept HasSetContext = requires(T& t, Context ctx) {
    t.setContext(ctx);
};

template <typename T>
concept HasInitialize = requires(T& t) {
    t.initialize();
};

template <typename T>
concept HasRunToken = requires(T& t, std::stop_token st) {
    t.run(st);
};

template <typename T>
concept HasStart = requires(T& t) {
    t.start();
};

template <typename T>
concept HasStop = requires(T& t) {
    t.stop();
};

template <typename T>
concept HasJoin = requires(T& t) {
    t.join();
};

} // namespace internal

/// @brief Non-allocating ServiceRegistry storing service handles in a fixed stack/static array.
/// Zero heap allocations, zero vtables/RTTI.
/// @tparam MaxServices Maximum number of background services allowed per registry (default 8).
/// @tparam EventVariant Supported event variant type list.
template <size_t MaxServices = 8, typename EventVariant = DefaultEvents>
class BasicServiceRegistry {
public:
    using ServiceContextType = ServiceContextT<EventVariant>;

    /// @brief Non-allocating type-erased handle for static background services.
    struct ServiceHandle {
        void* instance = nullptr;
        internal::TypeIdPtr typeId = nullptr;
        void (*initFn)(void* inst, ServiceContextType ctx) = nullptr;
        void (*startFn)(void* inst) = nullptr;
        void (*stopFn)(void* inst) noexcept = nullptr;
        void (*joinFn)(void* inst) noexcept = nullptr;
    };

    BasicServiceRegistry() = default;

    /// @brief Register a background service instance by reference.
    /// @tparam ServiceType Type of the background service.
    /// @param serviceInstance Reference to service instance.
    /// @return true if service was registered successfully; false if registry is full.
    template <typename ServiceType>
    bool registerService(ServiceType& serviceInstance)
    {
        if (_count >= MaxServices) {
            return false;
        }
        _services[_count] = ServiceHandle{
            &serviceInstance,
            internal::getTypeId<ServiceType>(),
            [](void* ptr, ServiceContextType ctx) {
                auto* s = static_cast<ServiceType*>(ptr);
                if constexpr (internal::HasSetContext<ServiceType, ServiceContextType>) { s->setContext(ctx); }
                if constexpr (internal::HasInitialize<ServiceType>) { s->initialize(); }
            },
            [](void* ptr) {
                auto* s = static_cast<ServiceType*>(ptr);
                if constexpr (internal::HasRunToken<ServiceType>) {
                    s->startThread(s);
                } else if constexpr (internal::HasStart<ServiceType>) {
                    s->start();
                }
            },
            [](void* ptr) noexcept {
                auto* s = static_cast<ServiceType*>(ptr);
                if constexpr (internal::HasStop<ServiceType>) { s->stop(); }
            },
            [](void* ptr) noexcept {
                auto* s = static_cast<ServiceType*>(ptr);
                if constexpr (internal::HasJoin<ServiceType>) { s->join(); }
            }
        };
        _count++;
        return true;
    }

    /// @brief Retrieve a registered service instance by static type ID.
    [[nodiscard]] void* getServiceById(internal::TypeIdPtr typeId) const noexcept
    {
        for (size_t i = 0; i < _count; ++i) {
            if (_services[i].typeId == typeId) {
                return _services[i].instance;
            }
        }
        return nullptr;
    }

    /// @brief Retrieve a registered service instance by concrete type.
    /// @tparam ServiceType Type of the background service.
    /// @return Pointer to registered ServiceType instance, or nullptr if not found.
    template <typename ServiceType>
    [[nodiscard]] ServiceType* getService() const noexcept
    {
        return static_cast<ServiceType*>(getServiceById(internal::getTypeId<ServiceType>()));
    }

    /// @brief Retrieve event sink handle of a target registered service if available.
    /// @tparam ServiceType Type of the target background service.
    /// @return EventSinkT handle targeting the service's incoming queue, or empty handle if unavailable.
    template <typename ServiceType>
    [[nodiscard]] EventSinkT<EventVariant> getServiceSink() const noexcept
    {
        auto* service = getService<ServiceType>();
        if (service) {
            if constexpr (requires { service->sink(); }) {
                return service->sink();
            }
        }
        return EventSinkT<EventVariant>{};
    }

    /// @brief Initialize and launch all registered background service jthreads.
    void initialize(ServiceContextType ctx)
    {
        ctx.setRegistry(const_cast<BasicServiceRegistry*>(this), [](void* regPtr, internal::TypeIdPtr typeId) -> void* {
            return static_cast<BasicServiceRegistry*>(regPtr)->getServiceById(typeId);
        });

        for (size_t i = 0; i < _count; ++i) {
            if (_services[i].initFn) {
                _services[i].initFn(_services[i].instance, ctx);
            }
            if (_services[i].startFn) {
                _services[i].startFn(_services[i].instance);
            }
        }
    }

    /// @brief Stop and join all registered background service threads.
    void shutdown() noexcept
    {
        for (size_t i = 0; i < _count; ++i) {
            if (_services[i].stopFn) {
                _services[i].stopFn(_services[i].instance);
            }
        }
        for (size_t i = 0; i < _count; ++i) {
            if (_services[i].joinFn) {
                _services[i].joinFn(_services[i].instance);
            }
        }
    }

    /// @brief Access number of registered services.
    [[nodiscard]] size_t size() const noexcept
    {
        return _count;
    }

private:
    std::array<ServiceHandle, MaxServices> _services{};
    size_t _count = 0;
};

/// @brief Default ServiceRegistry alias using MaxServices=8 and DefaultEvents.
using ServiceRegistry = BasicServiceRegistry<8, DefaultEvents>;

/// @brief Templated ServiceRegistry alias for custom capacity and event variant.
template <size_t MaxServices = 8, typename EventVariant = DefaultEvents>
using ServiceRegistryT = BasicServiceRegistry<MaxServices, EventVariant>;

} // namespace corium
