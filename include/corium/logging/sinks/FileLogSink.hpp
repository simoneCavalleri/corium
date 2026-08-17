#pragma once

#include <cstdio>
#include "corium/logging/LogEvent.hpp"

namespace corium::logging::sinks {

/// @brief File log sink appending formatted log entries to a designated file.
class FileLogSink {
public:
    FileLogSink() = default;

    explicit FileLogSink(const char* filePath)
    {
        open(filePath);
    }

    ~FileLogSink()
    {
        close();
    }

    FileLogSink(const FileLogSink&) = delete;
    FileLogSink& operator=(const FileLogSink&) = delete;

    FileLogSink(FileLogSink&& rhs) noexcept
    {
        _file = rhs._file;
        rhs._file = nullptr;
    }

    FileLogSink& operator=(FileLogSink&& rhs) noexcept
    {
        if (this != &rhs) {
            close();
            _file = rhs._file;
            rhs._file = nullptr;
        }
        return *this;
    }

    /// @brief Open target log file in append mode.
    bool open(const char* filePath)
    {
        close();
        if (filePath && filePath[0] != '\0') {
#if defined(_MSC_VER)
            ::fopen_s(&_file, filePath, "a");
#else
            _file = std::fopen(filePath, "a");
#endif
        }
        return _file != nullptr;
    }

    /// @brief Close target log file.
    void close() noexcept
    {
        if (_file) {
            std::fclose(_file);
            _file = nullptr;
        }
    }

    /// @brief Write log event entry to file.
    template <std::size_t N>
    void write(const LogEventT<N>& event) const
    {
        if (_file) {
            std::fprintf(_file, "[%s] [%s] %.*s\n",
                         logLevelToString(event.level),
                         event.category,
                         static_cast<int>(event.length),
                         event.message.data());
            std::fflush(_file);
        }
    }

    [[nodiscard]] bool isOpen() const noexcept
    {
        return _file != nullptr;
    }

private:
    std::FILE* _file = nullptr;
};

} // namespace corium::logging::sinks
