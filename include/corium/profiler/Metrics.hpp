/**
 * @file Metrics.hpp
 * @ingroup profiler
 * @brief Zero-heap Prometheus-compatible metric counters, gauges, and histograms.
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>

namespace corium::profiler {

/// @ingroup profiler
/// @brief Atomic 64-bit monotonically increasing counter.
class Counter {
public:
    explicit constexpr Counter(std::string_view name, std::string_view help = "") noexcept
        : m_name(name), m_help(help)
    {}

    /// @brief Increment counter value.
    /// @param val Increment amount (default: 1).
    void increment(uint64_t val = 1) noexcept {
        m_value.fetch_add(val, std::memory_order_relaxed);
    }

    /// @brief Current counter value.
    [[nodiscard]] uint64_t get() const noexcept {
        return m_value.load(std::memory_order_relaxed);
    }

    /// @brief Reset counter to zero.
    void reset() noexcept {
        m_value.store(0, std::memory_order_relaxed);
    }

    [[nodiscard]] std::string_view name() const noexcept { return m_name; }
    [[nodiscard]] std::string_view help() const noexcept { return m_help; }

private:
    std::string_view m_name;
    std::string_view m_help;
    std::atomic<uint64_t> m_value{0};
};

/// @ingroup profiler
/// @brief Atomic 64-bit signed gauge metric representing instantaneous level.
class Gauge {
public:
    explicit constexpr Gauge(std::string_view name, std::string_view help = "") noexcept
        : m_name(name), m_help(help)
    {}

    /// @brief Set gauge to an absolute value.
    void set(int64_t val) noexcept {
        m_value.store(val, std::memory_order_relaxed);
    }

    /// @brief Increment gauge.
    void increment(int64_t val = 1) noexcept {
        m_value.fetch_add(val, std::memory_order_relaxed);
    }

    /// @brief Decrement gauge.
    void decrement(int64_t val = 1) noexcept {
        m_value.fetch_sub(val, std::memory_order_relaxed);
    }

    /// @brief Current gauge value.
    [[nodiscard]] int64_t get() const noexcept {
        return m_value.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::string_view name() const noexcept { return m_name; }
    [[nodiscard]] std::string_view help() const noexcept { return m_help; }

private:
    std::string_view m_name;
    std::string_view m_help;
    std::atomic<int64_t> m_value{0};
};

/// @ingroup profiler
/// @brief Statically bucketed latency distribution histogram.
/// @tparam NumBuckets Number of upper boundary buckets (default: 8).
template <size_t NumBuckets = 8>
class Histogram {
public:
    constexpr Histogram(
        std::string_view name,
        std::array<double, NumBuckets> bounds,
        std::string_view help = ""
    ) noexcept
        : m_name(name), m_bounds(bounds), m_help(help)
    {}

    /// @brief Record an observed value.
    /// @param val Observation (e.g. latency in milliseconds or microseconds).
    void observe(double val) noexcept {
        m_count.fetch_add(1, std::memory_order_relaxed);

        for (size_t i = 0; i < NumBuckets; ++i) {
            if (val <= m_bounds[i]) {
                m_buckets[i].fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    [[nodiscard]] uint64_t count() const noexcept {
        return m_count.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t bucketCount(size_t index) const noexcept {
        return index < NumBuckets ? m_buckets[index].load(std::memory_order_relaxed) : 0;
    }

    [[nodiscard]] double bucketBound(size_t index) const noexcept {
        return index < NumBuckets ? m_bounds[index] : 0.0;
    }

    [[nodiscard]] std::string_view name() const noexcept { return m_name; }
    [[nodiscard]] std::string_view help() const noexcept { return m_help; }

private:
    std::string_view m_name;
    std::array<double, NumBuckets> m_bounds;
    std::string_view m_help;
    std::array<std::atomic<uint64_t>, NumBuckets> m_buckets{};
    std::atomic<uint64_t> m_count{0};
};

/// @ingroup profiler
/// @brief Format counter in Prometheus exposition text format into a char buffer.
inline size_t formatPrometheusCounter(const Counter& c, std::span<char> buf) noexcept {
    if (buf.size() < 64) return 0;
    int len = std::snprintf(
        buf.data(), buf.size(),
        "# HELP %.*s %.*s\n# TYPE %.*s counter\n%.*s %lu\n",
        static_cast<int>(c.name().size()), c.name().data(),
        static_cast<int>(c.help().size()), c.help().data(),
        static_cast<int>(c.name().size()), c.name().data(),
        static_cast<int>(c.name().size()), c.name().data(),
        static_cast<unsigned long>(c.get())
    );
    return len > 0 ? static_cast<size_t>(len) : 0;
}

/// @ingroup profiler
/// @brief Format gauge in Prometheus exposition text format into a char buffer.
inline size_t formatPrometheusGauge(const Gauge& g, std::span<char> buf) noexcept {
    if (buf.size() < 64) return 0;
    int len = std::snprintf(
        buf.data(), buf.size(),
        "# HELP %.*s %.*s\n# TYPE %.*s gauge\n%.*s %ld\n",
        static_cast<int>(g.name().size()), g.name().data(),
        static_cast<int>(g.help().size()), g.help().data(),
        static_cast<int>(g.name().size()), g.name().data(),
        static_cast<int>(g.name().size()), g.name().data(),
        static_cast<long>(g.get())
    );
    return len > 0 ? static_cast<size_t>(len) : 0;
}

} // namespace corium::profiler
