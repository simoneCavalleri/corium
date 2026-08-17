#pragma once

#include "corium/Service.hpp"
#include "corium/internal/VariantIndex.hpp"

#include <chrono>
#include <exception>
#include <stop_token>
#include <thread>

namespace corium {

/// @brief Non-allocating base class for background services owning a dedicated C++20 std::jthread.
/// Extends Service to add thread lifecycle management and thread-safe waitAndPump waiting.
/// Zero heap allocations, zero vtables/RTTI.
/// @tparam EventVariantType Supported event variant type list.
/// @tparam QueuePolicy Strategy for queueing incoming events (bounded lock-free MPSC).
/// @tparam SignalPolicy Strategy for signaling (CallbackSignalPolicy default).
/// @tparam StoragePolicy Strategy for compile-time handler capacity and delegate storage.
/// @tparam OverflowPolicy Strategy for queue overflow handling.
template <
    typename EventVariantType = DefaultEvents,
    typename QueuePolicy = BoundedMpscQueuePolicy<EventVariantType, 1024>,
    typename SignalPolicy = CallbackSignalPolicy,
    typename StoragePolicy = DefaultStoragePolicy,
    typename OverflowPolicy = DropNewestOverflowPolicy
>
class BackgroundService : public Service<EventVariantType, QueuePolicy, SignalPolicy, StoragePolicy, OverflowPolicy> {
public:
    using Base = Service<EventVariantType, QueuePolicy, SignalPolicy, StoragePolicy, OverflowPolicy>;
    using EventVariant = typename Base::EventVariant;

    BackgroundService() = default;

    explicit BackgroundService(ServiceContextT<EventVariant> context)
        : Base(context)
    {}

    ~BackgroundService()
    {
        stop();
        join();
    }

    BackgroundService(const BackgroundService&) = delete;
    BackgroundService& operator=(const BackgroundService&) = delete;

    BackgroundService(BackgroundService&&) noexcept = default;
    BackgroundService& operator=(BackgroundService&&) noexcept = default;

    /// @brief Wait for incoming events or timeout, then pump all available incoming events.
    /// Safe for use inside worker thread run(std::stop_token).
    /// @tparam Rep Duration representation type.
    /// @tparam Period Duration period type.
    /// @param stopToken std::stop_token from run(stopToken).
    /// @param timeout Maximum duration to wait if queue is empty.
    /// @return Number of events processed.
    template <typename Rep, typename Period>
    std::size_t waitAndPump(const std::stop_token& stopToken, const std::chrono::duration<Rep, Period>& timeout)
    {
        if (this->incomingBus().empty() && !stopToken.stop_requested()) {
            this->incomingBus().signalPolicy().wait_for(timeout);
        }

        std::size_t processed = 0;
        while (!stopToken.stop_requested()) {
            if (!this->incomingBus().processOne()) {
                break;
            }
            processed++;
        }
        return processed;
    }

    /// @brief Start execution loop on dedicated std::jthread.
    template <typename Derived>
    void startThread(Derived* derived)
    {
        _thread = std::jthread([this, derived](std::stop_token stopToken) {
#if __cpp_exceptions
            using EvVariant = EventVariantType;
            try {
                derived->run(stopToken);
            } catch (const std::exception& e) {
                if constexpr (requires { derived->onError(std::current_exception()); }) {
                    derived->onError(std::current_exception());
                } else if constexpr (requires { derived->onError(e.what()); }) {
                    derived->onError(e.what());
                }
                if constexpr (has_variant_type_v<ErrorEvent, EvVariant>) {
                    this->postHighPriority(ErrorEvent{1, reinterpret_cast<uintptr_t>(e.what())});
                }
            } catch (...) {
                if constexpr (requires { derived->onError(std::current_exception()); }) {
                    derived->onError(std::current_exception());
                }
                if constexpr (has_variant_type_v<ErrorEvent, EvVariant>) {
                    this->postHighPriority(ErrorEvent{1, 0});
                }
            }
#else
            derived->run(stopToken);
#endif
        });
    }

    /// @brief Request graceful stop of the background thread via std::stop_token.
    void stop() noexcept
    {
        _thread.request_stop();
    }

    /// @brief Join background std::jthread cleanly.
    void join() noexcept
    {
        if (_thread.joinable()) {
            _thread.join();
        }
    }

private:
    std::jthread _thread;
};

/// @brief Zero-overhead BackgroundService alias for producer worker threads (zero incoming queue/reactor footprint).
/// @tparam EventVariant Supported event variant type list.
template <typename EventVariant = DefaultEvents>
using ProducerBackgroundService = BackgroundService<
    EventVariant,
    NoQueuePolicy<EventVariant>,
    NoSignalPolicy,
    ZeroStoragePolicy
>;

/// @brief BackgroundService alias for consumer worker threads with configurable queue capacity.
/// @tparam EventVariant Supported event variant type list.
/// @tparam Capacity Incoming ring buffer event capacity.
template <typename EventVariant = DefaultEvents, std::size_t Capacity = 64>
using ConsumerBackgroundService = BackgroundService<
    EventVariant,
    BoundedMpscQueuePolicy<EventVariant, Capacity>,
    CallbackSignalPolicy,
    DefaultStoragePolicy
>;

} // namespace corium
