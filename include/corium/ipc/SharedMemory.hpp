#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace corium::ipc {

/// @ingroup ipc
/// @brief Non-allocating wrapper for fixed raw memory regions (e.g. multi-core SRAM, DMA buffers, hardware shared RAM).
class RawMemoryBuffer {
public:
    constexpr RawMemoryBuffer() noexcept = default;

    constexpr RawMemoryBuffer(void* address, std::size_t size, bool isCreator = false) noexcept
        : _address(address), _size(size), _isCreator(isCreator)
    {}

    [[nodiscard]] void* data() noexcept { return _address; }
    [[nodiscard]] const void* data() const noexcept { return _address; }
    [[nodiscard]] std::size_t size() const noexcept { return _size; }
    [[nodiscard]] bool isValid() const noexcept { return _address != nullptr; }
    [[nodiscard]] bool isCreator() const noexcept { return _isCreator; }

private:
    void* _address{nullptr};
    std::size_t _size{0};
    bool _isCreator{false};
};

/// @ingroup ipc
/// @brief Cross-platform zero-copy shared memory region wrapper.
/// Manages OS-level shared memory allocation, memory mapping, and cleanup.
class SharedMemory {
public:
    enum class AccessMode {
        CreateOrOpen,
        OpenReadOnly,
        OpenReadWrite
    };

    SharedMemory() noexcept = default;

    ~SharedMemory()
    {
        close();
    }

    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    SharedMemory(SharedMemory&& other) noexcept
        : _name(std::move(other._name)),
          _address(other._address),
          _size(other._size),
          _isCreator(other._isCreator)
#if defined(_WIN32) || defined(_WIN64)
        , _handle(other._handle)
#else
        , _fd(other._fd)
#endif
    {
        other._address = nullptr;
        other._size = 0;
        other._isCreator = false;
#if defined(_WIN32) || defined(_WIN64)
        other._handle = nullptr;
#else
        other._fd = -1;
#endif
    }

    SharedMemory& operator=(SharedMemory&& other) noexcept
    {
        if (this != &other) {
            close();
            _name = std::move(other._name);
            _address = other._address;
            _size = other._size;
            _isCreator = other._isCreator;
#if defined(_WIN32) || defined(_WIN64)
            _handle = other._handle;
            other._handle = nullptr;
#else
            _fd = other._fd;
            other._fd = -1;
#endif
            other._address = nullptr;
            other._size = 0;
            other._isCreator = false;
        }
        return *this;
    }

    /// @brief Create or attach to a shared memory region.
    /// @param name Unique system identifier (e.g., "/corium_telemetry_shm").
    /// @param size Required memory segment capacity in bytes.
    /// @param mode Allocation/Access mode.
    /// @return true on success, false otherwise.
    bool open(const std::string& name, std::size_t size, AccessMode mode = AccessMode::CreateOrOpen) noexcept
    {
        close();
        _name = normalizeName(name);
        _size = size;

#if defined(_WIN32) || defined(_WIN64)
        DWORD protect = (mode == AccessMode::OpenReadOnly) ? PAGE_READONLY : PAGE_READWRITE;
        DWORD desiredAccess = (mode == AccessMode::OpenReadOnly) ? FILE_MAP_READ : FILE_MAP_ALL_ACCESS;

        if (mode == AccessMode::CreateOrOpen) {
            _handle = CreateFileMappingA(
                INVALID_HANDLE_VALUE,
                nullptr,
                protect,
                static_cast<DWORD>(size >> 32),
                static_cast<DWORD>(size & 0xFFFFFFFF),
                _name.c_str()
            );
            if (_handle) {
                _isCreator = (GetLastError() != ERROR_ALREADY_EXISTS);
            }
        } else {
            _handle = OpenFileMappingA(desiredAccess, FALSE, _name.c_str());
            _isCreator = false;
        }

        if (!_handle) {
            return false;
        }

        _address = MapViewOfFile(_handle, desiredAccess, 0, 0, size);
        if (!_address) {
            CloseHandle(_handle);
            _handle = nullptr;
            return false;
        }
        return true;
#else
        int oflag = 0;
        mode_t permissions = 0666;

        if (mode == AccessMode::CreateOrOpen) {
            oflag = O_CREAT | O_RDWR;
        } else if (mode == AccessMode::OpenReadOnly) {
            oflag = O_RDONLY;
        } else {
            oflag = O_RDWR;
        }

        _fd = ::shm_open(_name.c_str(), oflag, permissions);
        if (_fd < 0) {
            return false;
        }

        if (mode == AccessMode::CreateOrOpen) {
            struct stat sb{};
            if (::fstat(_fd, &sb) == 0 && sb.st_size < static_cast<off_t>(size)) {
                if (::ftruncate(_fd, static_cast<off_t>(size)) != 0) {
                    ::close(_fd);
                    _fd = -1;
                    return false;
                }
                _isCreator = true;
            }
        }

        int prot = (mode == AccessMode::OpenReadOnly) ? PROT_READ : (PROT_READ | PROT_WRITE);
        _address = ::mmap(nullptr, size, prot, MAP_SHARED, _fd, 0);
        if (_address == MAP_FAILED) {
            _address = nullptr;
            ::close(_fd);
            _fd = -1;
            return false;
        }
        return true;
#endif
    }

    /// @brief Close and unmap the shared memory segment.
    void close() noexcept
    {
        if (_address) {
#if defined(_WIN32) || defined(_WIN64)
            UnmapViewOfFile(_address);
            if (_handle) {
                CloseHandle(_handle);
                _handle = nullptr;
            }
#else
            ::munmap(_address, _size);
            if (_fd >= 0) {
                ::close(_fd);
                _fd = -1;
            }
#endif
            _address = nullptr;
            _size = 0;
            _isCreator = false;
        }
    }

    /// @brief Remove shared memory identifier from OS namespace (POSIX shm_unlink).
    static void unlink(const std::string& name) noexcept
    {
#if !defined(_WIN32) && !defined(_WIN64)
        std::string n = normalizeName(name);
        ::shm_unlink(n.c_str());
#else
        (void)name;
#endif
    }

    /// @brief Raw pointer to mapped shared memory buffer.
    [[nodiscard]] void* data() noexcept { return _address; }
    [[nodiscard]] const void* data() const noexcept { return _address; }

    /// @brief Size of mapped shared memory buffer.
    [[nodiscard]] std::size_t size() const noexcept { return _size; }

    /// @brief Check if mapped region is valid.
    [[nodiscard]] bool isValid() const noexcept { return _address != nullptr; }

    /// @brief Check if this process created the memory segment (useful for initialization).
    [[nodiscard]] bool isCreator() const noexcept { return _isCreator; }

    /// @brief Name identifier of shared memory.
    [[nodiscard]] const std::string& name() const noexcept { return _name; }

private:
    static std::string normalizeName(const std::string& name)
    {
#if !defined(_WIN32) && !defined(_WIN64)
        if (name.empty() || name[0] != '/') {
            return "/" + name;
        }
#endif
        return name;
    }

    std::string _name;
    void* _address{nullptr};
    std::size_t _size{0};
    bool _isCreator{false};

#if defined(_WIN32) || defined(_WIN64)
    HANDLE _handle{nullptr};
#else
    int _fd{-1};
#endif
};

} // namespace corium::ipc
