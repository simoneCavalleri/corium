#pragma once

#include "corium/IEventSink.hpp"
#include "corium/internal/VariantIndex.hpp"

namespace corium {

/// @brief Execution context provided to background services.
/// Allows services to post events back to the main event queue thread-safely
/// and communicate with other registered background services.
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
struct ServiceContextT {
    using GetServiceFn = void* (*)(void* registryPtr, internal::TypeIdPtr typeId);

    IEventSinkT<EventVariant> eventSink;
    void* registryPtr = nullptr;
    GetServiceFn getServiceFn = nullptr;

    /// @brief Retrieve another registered service instance by type.
    /// @tparam ServiceType Type of the target background service.
    /// @return Pointer to ServiceType instance or nullptr if not registered.
    template <typename ServiceType>
    [[nodiscard]] ServiceType* getService() const noexcept
    {
        if (registryPtr && getServiceFn) {
            return static_cast<ServiceType*>(getServiceFn(registryPtr, internal::getTypeId<ServiceType>()));
        }
        return nullptr;
    }

    /// @brief Retrieve event sink handle of a target registered service if available.
    /// @tparam ServiceType Type of the target background service.
    /// @return IEventSinkT handle targeting the service's incoming queue, or empty handle if unavailable.
    template <typename ServiceType>
    [[nodiscard]] IEventSinkT<EventVariant> getServiceSink() const noexcept
    {
        auto* service = getService<ServiceType>();
        if (service) {
            if constexpr (requires { service->sink(); }) {
                return service->sink();
            } else if constexpr (requires { service->serviceSink(); }) {
                return service->serviceSink();
            }
        }
        return IEventSinkT<EventVariant>{};
    }

    /// @brief Post an event directly to a target service.
    /// @tparam TargetService Target service type to send the event to.
    /// @tparam EventType Event type to send.
    /// @param event Event instance to post.
    /// @param priority Priority level.
    /// @return true if event was posted to the target service; false if target service was not found or has no sink.
    template <typename TargetService, typename EventType>
    bool sendToService(EventType&& event, EventPriority priority = EventPriority::Normal) const
    {
        auto sinkHandle = getServiceSink<TargetService>();
        if (sinkHandle) {
            sinkHandle.post(std::forward<EventType>(event), priority);
            return true;
        }
        return false;
    }
};

/// @brief Default ServiceContext alias using DefaultEvents.
using ServiceContext = ServiceContextT<DefaultEvents>;

} // namespace corium
