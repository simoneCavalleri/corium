/**
 * @file DomainSocket.hpp
 * @ingroup ipc
 * @brief UNIX Domain Socket datagram listener and client implementation.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <afunix.h>
#define CORIUM_HAS_POSIX_SOCKETS 0
#elif __has_include(<sys/socket.h>) && __has_include(<sys/un.h>) && __has_include(<unistd.h>) && __has_include(<fcntl.h>)
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#define CORIUM_HAS_POSIX_SOCKETS 1
#else
#define CORIUM_HAS_POSIX_SOCKETS 0
#endif

namespace corium::ipc {

/// @brief Low-level RAII abstraction for UNIX Domain Datagram Sockets (AF_UNIX / SOCK_DGRAM).
/// Provides discrete, boundary-preserving, zero-fragmentation message passing.
class DomainSocket {
public:
    DomainSocket() noexcept = default;

    ~DomainSocket()
    {
        close();
    }

    DomainSocket(const DomainSocket&) = delete;
    DomainSocket& operator=(const DomainSocket&) = delete;

    DomainSocket(DomainSocket&& other) noexcept
        : _fd(other._fd),
          _boundPath(std::move(other._boundPath)),
          _isBound(other._isBound)
    {
        other._fd = -1;
        other._isBound = false;
    }

    DomainSocket& operator=(DomainSocket&& other) noexcept
    {
        if (this != &other) {
            close();
            _fd = other._fd;
            _boundPath = std::move(other._boundPath);
            _isBound = other._isBound;
            other._fd = -1;
            other._isBound = false;
        }
        return *this;
    }

    /// @brief Bind socket to a filesystem path as a receiving server.
    /// @param socketPath Filesystem path for the socket (e.g. "/tmp/corium_daemon.sock").
    /// @return true on success, false otherwise.
    bool bind(const std::string& socketPath) noexcept
    {
        close();

#if CORIUM_HAS_POSIX_SOCKETS
        ::unlink(socketPath.c_str());

        _fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
        if (_fd < 0) {
            return false;
        }

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

        if (::bind(_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(_fd);
            _fd = -1;
            return false;
        }

        _boundPath = socketPath;
        _isBound = true;
        return true;
#else
        (void)socketPath;
        return false;
#endif
    }

    /// @brief Connect to a destination socket as a client.
    /// @param socketPath Filesystem path of the target server socket.
    /// @param clientPath Optional path to bind this client for bidirectional responses.
    /// @return true on success, false otherwise.
    bool connect(const std::string& socketPath, const std::string& clientPath = "") noexcept
    {
        close();

#if CORIUM_HAS_POSIX_SOCKETS
        _fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
        if (_fd < 0) {
            return false;
        }

        if (!clientPath.empty()) {
            ::unlink(clientPath.c_str());
            struct sockaddr_un localAddr{};
            localAddr.sun_family = AF_UNIX;
            std::strncpy(localAddr.sun_path, clientPath.c_str(), sizeof(localAddr.sun_path) - 1);

            if (::bind(_fd, reinterpret_cast<struct sockaddr*>(&localAddr), sizeof(localAddr)) != 0) {
                ::close(_fd);
                _fd = -1;
                return false;
            }
            _boundPath = clientPath;
            _isBound = true;
        }

        struct sockaddr_un remoteAddr{};
        remoteAddr.sun_family = AF_UNIX;
        std::strncpy(remoteAddr.sun_path, socketPath.c_str(), sizeof(remoteAddr.sun_path) - 1);

        if (::connect(_fd, reinterpret_cast<struct sockaddr*>(&remoteAddr), sizeof(remoteAddr)) != 0) {
            close();
            return false;
        }
        return true;
#else
        (void)socketPath;
        (void)clientPath;
        return false;
#endif
    }

    /// @brief Set socket non-blocking mode.
    void setNonBlocking(bool nonBlocking) noexcept
    {
#if CORIUM_HAS_POSIX_SOCKETS
        if (_fd >= 0) {
            int flags = ::fcntl(_fd, F_GETFL, 0);
            if (flags >= 0) {
                if (nonBlocking) {
                    flags |= O_NONBLOCK;
                } else {
                    flags &= ~O_NONBLOCK;
                }
                ::fcntl(_fd, F_SETFL, flags);
            }
        }
#else
        (void)nonBlocking;
#endif
    }

    /// @brief Send datagram packet over the connected socket.
    /// @param buffer Pointer to payload.
    /// @param length Payload length in bytes.
    /// @return Number of bytes sent, or -1 on error.
    int send(const void* buffer, std::size_t length) noexcept
    {
#if CORIUM_HAS_POSIX_SOCKETS
        if (_fd < 0) return -1;
        return static_cast<int>(::send(_fd, buffer, length, 0));
#else
        (void)buffer;
        (void)length;
        return -1;
#endif
    }

    /// @brief Receive datagram packet from socket.
    /// @param buffer Destination buffer.
    /// @param maxLength Maximum bytes to receive.
    /// @return Number of bytes received, or -1 on error/EWOULDBLOCK.
    int receive(void* buffer, std::size_t maxLength) noexcept
    {
#if CORIUM_HAS_POSIX_SOCKETS
        if (_fd < 0) return -1;
        return static_cast<int>(::recv(_fd, buffer, maxLength, 0));
#else
        (void)buffer;
        (void)maxLength;
        return -1;
#endif
    }

    /// @brief Close socket and unlink bound file if server.
    void close() noexcept
    {
#if CORIUM_HAS_POSIX_SOCKETS
        if (_fd >= 0) {
            ::close(_fd);
            _fd = -1;
        }
        if (_isBound && !_boundPath.empty()) {
            ::unlink(_boundPath.c_str());
            _isBound = false;
            _boundPath.clear();
        }
#endif
    }

    /// @brief Check if socket descriptor is open.
    [[nodiscard]] bool isOpen() const noexcept { return _fd >= 0; }

    /// @brief Access underlying OS file descriptor.
    [[nodiscard]] int nativeHandle() const noexcept { return _fd; }

private:
    int _fd{-1};
    std::string _boundPath;
    bool _isBound{false};
};

} // namespace corium::ipc
