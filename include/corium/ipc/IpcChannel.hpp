/**
 * @file IpcChannel.hpp
 * @ingroup ipc
 * @brief Zero-copy typed event exchange over POSIX shared memory.
 */

#pragma once

#include <cstddef>
#include <string>
#include <utility>

#include "corium/Events.hpp"
#include "corium/EventSink.hpp"
#include "corium/ipc/SharedMemory.hpp"
#include "corium/ipc/ShmMpscQueue.hpp"

namespace corium::ipc {

/// @ingroup ipc
/// @brief High-level typed inter-process communication channel for Corium events.
/// Encapsulates OS shared memory allocation and lock-free event dispatching.
/// @tparam EventVariant Supported event variant list (must be trivially copyable or POD).
/// @tparam Capacity Ring buffer capacity (power of 2, default 256).
template <
    typename EventVariant = DefaultEvents,
    std::size_t Capacity = 256
>
class IpcChannel {
    static_assert(std::is_trivially_copyable_v<EventVariant>,
        "IpcChannel EventVariant must be trivially copyable for zero-copy shared memory IPC.");

public:
    using QueueType = ShmMpscQueue<EventVariant, Capacity>;

    IpcChannel() = default;

    /// @brief Create a new shared memory channel as the host/creator process.
    /// @param channelName Unique system name (e.g. "/corium_robot_telemetry").
    /// @return true on success, false otherwise.
    bool create(const std::string& channelName) noexcept
    {
        if (!_shm.open(channelName, QueueType::requiredMemorySize(), SharedMemory::AccessMode::CreateOrOpen)) {
            return false;
        }
        _queue.bind(_shm.data(), true);
        return _queue.isValid();
    }

    /// @brief Attach to an existing shared memory channel as a client process.
    /// @param channelName Unique system name.
    /// @return true on success, false otherwise.
    bool attach(const std::string& channelName) noexcept
    {
        if (!_shm.open(channelName, QueueType::requiredMemorySize(), SharedMemory::AccessMode::OpenReadWrite)) {
            return false;
        }
        _queue.bind(_shm.data(), false);
        return _queue.isValid();
    }

    /// @brief Bind channel directly to a raw memory buffer (e.g. multi-core embedded SRAM).
    /// @param buffer Pointer to raw memory region.
    /// @param isCreator If true, initializes queue headers; if false, attaches to existing queue.
    /// @return true if valid, false if buffer is null.
    bool bindRaw(void* buffer, bool isCreator = true) noexcept
    {
        _queue.bind(buffer, isCreator);
        return _queue.isValid();
    }

    /// @brief Post an event into the shared memory queue for remote processes.
    /// Lock-free, zero-allocation, multi-producer safe.
    /// @tparam EventType Strongly-typed event type.
    /// @param event Event payload.
    /// @return true if pushed, false if shared queue is full.
    template <typename EventType>
    bool post(EventType&& event) noexcept
    {
        return _queue.tryPush(EventVariant{std::forward<EventType>(event)});
    }

    /// @brief Pop one event from the shared queue.
    /// Single-consumer safe.
    /// @param outEvent Reference populated with popped event variant.
    /// @return true if an event was popped, false if queue is empty.
    bool tryPop(EventVariant& outEvent) noexcept
    {
        return _queue.tryPop(outEvent);
    }

    /// @brief Drain incoming shared memory events into a target event sink.
    /// @tparam SinkType Target EventSink or compatible sink.
    /// @param sink Target sink instance.
    /// @param maxEvents Maximum events to drain (0 = drain all pending).
    /// @return Number of events successfully transferred into local runtime.
    template <typename SinkType>
    std::size_t pumpInto(const SinkType& sink, std::size_t maxEvents = 0)
    {
        std::size_t count = 0;
        EventVariant ev;
        while (_queue.tryPop(ev)) {
            sink.post(std::move(ev));
            count++;
            if (maxEvents > 0 && count >= maxEvents) {
                break;
            }
        }
        return count;
    }

    /// @brief Destroy the channel and remove shared memory from OS.
    void unlink() noexcept
    {
        if (_shm.isValid()) {
            std::string name = _shm.name();
            _shm.close();
            SharedMemory::unlink(name);
        }
    }

    /// @brief Check if channel is open and valid.
    [[nodiscard]] bool isValid() const noexcept
    {
        return _shm.isValid() && _queue.isValid();
    }

    /// @brief Check if channel is empty.
    [[nodiscard]] bool empty() const noexcept
    {
        return _queue.empty();
    }

    /// @brief Configured capacity of the channel.
    [[nodiscard]] constexpr std::size_t capacity() const noexcept
    {
        return Capacity;
    }

private:
    SharedMemory _shm;
    QueueType _queue;
};

} // namespace corium::ipc
