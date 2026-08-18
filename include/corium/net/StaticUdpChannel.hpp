/**
 * @file StaticUdpChannel.hpp
 * @ingroup net
 * @brief Zero-heap UDP network channel for distributed event telemetry and IoT nodes.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#if defined(_WIN32) || defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define CORIUM_HAS_UDP_SOCKETS 1
#elif __has_include(<sys/socket.h>) && __has_include(<netinet/in.h>) && __has_include(<arpa/inet.h>) && __has_include(<unistd.h>) && __has_include(<fcntl.h>)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define CORIUM_HAS_UDP_SOCKETS 1
#else
#define CORIUM_HAS_UDP_SOCKETS 0
#endif

#include "corium/policies/QueuePolicies.hpp"
#include "corium/wire/Serializer.hpp"
#include "corium/wire/WirePacket.hpp"

namespace corium::net {

/// @ingroup net
/// @brief Statically buffered, zero-heap UDP communication channel for distributed Corium nodes.
/// @tparam MaxPacketSize Maximum UDP datagram payload size in bytes (default: 512).
template <size_t MaxPacketSize = 512>
class StaticUdpChannel {
public:
    constexpr StaticUdpChannel() noexcept = default;

    ~StaticUdpChannel() {
        close();
    }

    StaticUdpChannel(const StaticUdpChannel&) = delete;
    StaticUdpChannel& operator=(const StaticUdpChannel&) = delete;

    StaticUdpChannel(StaticUdpChannel&& other) noexcept
        : m_fd(other.m_fd) {
        other.m_fd = -1;
    }

    StaticUdpChannel& operator=(StaticUdpChannel&& other) noexcept {
        if (this != &other) {
            close();
            m_fd = other.m_fd;
            other.m_fd = -1;
        }
        return *this;
    }

    /// @brief Open UDP socket and bind to a local port and IP address.
    /// @param port Local port to bind to (0 for ephemeral).
    /// @param ip Local interface IP address (default: "0.0.0.0").
    /// @return true if socket was created and bound successfully.
    bool openAndBind(uint16_t port = 0, const char* ip = "0.0.0.0") noexcept {
#if CORIUM_HAS_UDP_SOCKETS
        close();

#if defined(_WIN32) || defined(_WIN64)
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) {
            return false;
        }
        m_fd = static_cast<int>(sock);
#else
        m_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (m_fd < 0) {
            return false;
        }
#endif

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
#if defined(_WIN32) || defined(_WIN64)
        InetPtonA(AF_INET, ip, &addr.sin_addr);
#else
        inet_pton(AF_INET, ip, &addr.sin_addr);
#endif

        if (::bind(m_fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
            close();
            return false;
        }
        return true;
#else
        (void)port;
        (void)ip;
        return false;
#endif
    }

    /// @brief Set socket non-blocking mode.
    /// @param nonBlocking true for non-blocking I/O.
    /// @return true on success.
    bool setNonBlocking(bool nonBlocking = true) noexcept {
#if CORIUM_HAS_UDP_SOCKETS
        if (m_fd < 0) {
            return false;
        }
#if defined(_WIN32) || defined(_WIN64)
        u_long mode = nonBlocking ? 1 : 0;
        return ioctlsocket(static_cast<SOCKET>(m_fd), FIONBIO, &mode) == 0;
#else
        int flags = fcntl(m_fd, F_GETFL, 0);
        if (flags < 0) return false;
        flags = nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
        return fcntl(m_fd, F_SETFL, flags) == 0;
#endif
#else
        (void)nonBlocking;
        return false;
#endif
    }

    /// @brief Send raw payload to target UDP endpoint.
    /// @param ip Target IPv4 address string (e.g. "127.0.0.1").
    /// @param port Target port.
    /// @param data Byte payload to transmit.
    /// @return true if datagram was sent successfully.
    bool sendTo(const char* ip, uint16_t port, std::span<const uint8_t> data) noexcept {
#if CORIUM_HAS_UDP_SOCKETS
        if (m_fd < 0) {
            // Lazy socket creation if not bound
            if (!openAndBind(0, "0.0.0.0")) {
                return false;
            }
        }

        sockaddr_in destAddr{};
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(port);
#if defined(_WIN32) || defined(_WIN64)
        InetPtonA(AF_INET, ip, &destAddr.sin_addr);
        int sent = ::sendto(
            static_cast<SOCKET>(m_fd),
            reinterpret_cast<const char*>(data.data()),
            static_cast<int>(data.size()),
            0,
            reinterpret_cast<const sockaddr*>(&destAddr),
            sizeof(destAddr)
        );
        return sent > 0 && static_cast<size_t>(sent) == data.size();
#else
        inet_pton(AF_INET, ip, &destAddr.sin_addr);
        auto sent = ::sendto(
            m_fd,
            data.data(),
            data.size(),
            0,
            reinterpret_cast<const sockaddr*>(&destAddr),
            sizeof(destAddr)
        );
        return sent > 0 && static_cast<size_t>(sent) == data.size();
#endif
#else
        (void)ip;
        (void)port;
        (void)data;
        return false;
#endif
    }

    /// @brief Serialize and send a typed event over UDP using Corium WirePacket framing.
    /// @tparam Event Concrete event type (must be trivially copyable).
    /// @tparam EventVariant Variant list for type indexing.
    /// @param ip Target IPv4 address.
    /// @param port Target port.
    /// @param event Event instance to transmit.
    /// @return true if packet was serialized and sent.
    template <typename Event, typename EventVariant>
    bool sendEvent(const char* ip, uint16_t port, const Event& event) noexcept {
        auto packet = corium::wire::WireSerializer::serialize<Event, EventVariant, MaxPacketSize>(event);
        return sendTo(ip, port, std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(&packet), packet.totalWireSize()));
    }

    /// @brief Receive raw bytes from incoming UDP datagram.
    /// @param bufferOut Output buffer.
    /// @param bytesReceivedOut Number of bytes received.
    /// @return true if datagram was received.
    bool receive(std::span<uint8_t> bufferOut, size_t& bytesReceivedOut) noexcept {
#if CORIUM_HAS_UDP_SOCKETS
        if (m_fd < 0) {
            bytesReceivedOut = 0;
            return false;
        }

#if defined(_WIN32) || defined(_WIN64)
        int recvd = ::recvfrom(
            static_cast<SOCKET>(m_fd),
            reinterpret_cast<char*>(bufferOut.data()),
            static_cast<int>(bufferOut.size()),
            0, nullptr, nullptr
        );
#else
        auto recvd = ::recvfrom(
            m_fd,
            bufferOut.data(),
            bufferOut.size(),
            0, nullptr, nullptr
        );
#endif
        if (recvd <= 0) {
            bytesReceivedOut = 0;
            return false;
        }
        bytesReceivedOut = static_cast<size_t>(recvd);
        return true;
#else
        (void)bufferOut;
        bytesReceivedOut = 0;
        return false;
#endif
    }

    /// @brief Receive a WirePacket and deserialize directly into a Corium EventSink.
    /// @tparam EventVariant Variant list of all supported events.
    /// @tparam Sink Target EventSink or EventBus.
    /// @param sink Target sink.
    /// @param priority Priority to assign to the deserialized event.
    /// @return true if a valid event packet was received and pushed into sink.
    template <typename EventVariant, typename Sink>
    bool receiveAndPush(Sink& sink, EventPriority priority = EventPriority::Normal) noexcept {
        size_t recvd = 0;
        if (!receive(std::span<uint8_t>(m_rxBuffer.data(), m_rxBuffer.size()), recvd)) {
            return false;
        }

        if (recvd < sizeof(corium::wire::WireHeader)) {
            return false;
        }

        corium::wire::WirePacket<MaxPacketSize> packet{};
        std::memcpy(&packet, m_rxBuffer.data(), recvd > sizeof(packet) ? sizeof(packet) : recvd);
        return corium::wire::WireSerializer::deserializeAndPush<EventVariant, MaxPacketSize, Sink>(
            packet, sink, priority);
    }

    /// @brief Close underlying socket.
    void close() noexcept {
#if CORIUM_HAS_UDP_SOCKETS
        if (m_fd >= 0) {
#if defined(_WIN32) || defined(_WIN64)
            closesocket(static_cast<SOCKET>(m_fd));
#else
            ::close(m_fd);
#endif
            m_fd = -1;
        }
#endif
    }

    /// @brief Returns true if socket is open and bound.
    [[nodiscard]] bool isOpen() const noexcept {
        return m_fd >= 0;
    }

    /// @brief Native socket file descriptor or handle.
    [[nodiscard]] int nativeHandle() const noexcept {
        return m_fd;
    }

private:
    int m_fd{-1};
    std::array<uint8_t, sizeof(corium::wire::WireHeader) + MaxPacketSize> m_rxBuffer{};
};

} // namespace corium::net
