#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#include <variant>

#include "corium/Events.hpp"
#include "corium/EventSink.hpp"
#include "corium/ipc/DomainSocket.hpp"

namespace corium::ipc {

/// @ingroup ipc
/// @brief Typed IPC channel operating over UNIX Domain Datagram Sockets.
/// Provides boundary-preserving, discrete event transmission with zero packet fragmentation.
/// @tparam EventVariant Supported event variant list (must be trivially copyable or POD).
/// @tparam MaxPacketSize Maximum serialized datagram packet buffer size (default 512 bytes).
template <
    typename EventVariant = DefaultEvents,
    std::size_t MaxPacketSize = 512
>
class UdsChannel {
    static_assert(sizeof(EventVariant) <= MaxPacketSize, "EventVariant size exceeds MaxPacketSize");
    static_assert(std::is_trivially_copyable_v<EventVariant>,
        "UdsChannel EventVariant must be trivially copyable for datagram socket transmission.");

public:
    UdsChannel() = default;

    /// @brief Start listening as an IPC server on a filesystem socket path.
    /// @param socketPath Filesystem path (e.g. "/tmp/corium_control.sock").
    /// @param nonBlocking If true, enables non-blocking mode for non-waiting event loop polling.
    /// @return true on success, false otherwise.
    bool listen(const std::string& socketPath, bool nonBlocking = true) noexcept
    {
        if (!_socket.bind(socketPath)) {
            return false;
        }
        if (nonBlocking) {
            _socket.setNonBlocking(true);
        }
        return true;
    }

    /// @brief Connect as a client to a server socket path.
    /// @param serverPath Server destination socket path.
    /// @param clientPath Optional client socket path for bidirectional reply.
    /// @return true on success, false otherwise.
    bool connect(const std::string& serverPath, const std::string& clientPath = "") noexcept
    {
        return _socket.connect(serverPath, clientPath);
    }

    /// @brief Post a typed event over the socket to the remote receiver.
    /// @tparam EventType Event type.
    /// @param event Event payload.
    /// @return true if datagram sent, false on socket error.
    template <typename EventType>
    bool post(EventType&& event) noexcept
    {
        EventVariant ev{std::forward<EventType>(event)};
        const int bytesSent = _socket.send(&ev, sizeof(EventVariant));
        return bytesSent == static_cast<int>(sizeof(EventVariant));
    }

    /// @brief Try popping one pending datagram event from the socket (non-blocking).
    /// @param outEvent Reference populated with received event variant.
    /// @return true if an event was received, false if socket has no pending datagrams.
    bool tryPop(EventVariant& outEvent) noexcept
    {
        const int bytesReceived = _socket.receive(&outEvent, sizeof(EventVariant));
        return bytesReceived == static_cast<int>(sizeof(EventVariant));
    }

    /// @brief Drain incoming UNIX domain socket events into a target event sink.
    /// @tparam SinkType Target EventSink or compatible sink.
    /// @param sink Target sink instance.
    /// @param maxEvents Maximum events to drain (0 = drain all pending).
    /// @return Number of events successfully transferred into local runtime.
    template <typename SinkType>
    std::size_t pumpInto(const SinkType& sink, std::size_t maxEvents = 0)
    {
        std::size_t count = 0;
        EventVariant ev;
        while (tryPop(ev)) {
            sink.post(std::move(ev));
            count++;
            if (maxEvents > 0 && count >= maxEvents) {
                break;
            }
        }
        return count;
    }

    /// @brief Close the socket connection and unlink socket file if server.
    void close() noexcept
    {
        _socket.close();
    }

    /// @brief Check if socket is open.
    [[nodiscard]] bool isOpen() const noexcept
    {
        return _socket.isOpen();
    }

    /// @brief Access underlying DomainSocket instance.
    [[nodiscard]] DomainSocket& socket() noexcept
    {
        return _socket;
    }

private:
    DomainSocket _socket;
};

} // namespace corium::ipc
