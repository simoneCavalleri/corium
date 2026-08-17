#pragma once

#include "corium/EventSink.hpp"
#include "corium/internal/VariantIndex.hpp"

namespace corium {

/// @brief Execution context provided to background services.
/// Allows services to post events back to the main event queue thread-safely
/// and communicate with other registered background services.
/// @tparam EventVariant The variant type list of supported events.
template <typename EventVariant = DefaultEvents>
class BasicServiceContext {
public:
    using GetServiceFn = void* (*)(void* registryPtr, internal::TypeIdPtr typeId);

    BasicServiceContext() = default;

    explicit BasicServiceContext(
        EventSinkT<EventVariant> sink,
        void* regPtr = nullptr,
        GetServiceFn fn = nullptr
    ) noexcept
        : _eventSink(sink), _registryPtr(regPtr), _getServiceFn(fn)
    {}

    /// @brief Access main application EventSink handle.
    [[nodiscard]] EventSinkT<EventVariant> mainSink() const noexcept
    {
        return _eventSink;
    }

    /// @brief Access main application EventSink handle.
    [[nodiscard]] EventSinkT<EventVariant> eventSink() const noexcept
    {
        return _eventSink;
    }

    /// @brief Configure registry resolution bridge (internal framework usage).
    void setRegistry(void* registryPtr, GetServiceFn fn) noexcept
    {
        _registryPtr = registryPtr;
        _getServiceFn = fn;
    }

    /// @brief Retrieve another registered service instance by type.
    /// @tparam ServiceType Type of the target background service.
    /// @return Pointer to ServiceType instance or nullptr if not registered.
    template <typename ServiceType>
    [[nodiscard]] ServiceType* getService() const noexcept
    {
        if (_registryPtr && _getServiceFn) {
            return static_cast<ServiceType*>(_getServiceFn(_registryPtr, internal::getTypeId<ServiceType>()));
        }
        return nullptr;
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

private:
    EventSinkT<EventVariant> _eventSink;
    void* _registryPtr = nullptr;
    GetServiceFn _getServiceFn = nullptr;
};

/// @brief Default ServiceContext alias using DefaultEvents.
using ServiceContext = BasicServiceContext<DefaultEvents>;

/// @brief Templated ServiceContext alias for custom event variant.
template <typename EventVariant = DefaultEvents>
using ServiceContextT = BasicServiceContext<EventVariant>;

} // namespace corium
