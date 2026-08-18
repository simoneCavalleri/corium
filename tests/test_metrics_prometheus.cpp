#include <gtest/gtest.h>
#include <array>
#include <string>

#include "corium/profiler/Metrics.hpp"

TEST(MetricsPrometheusTest, CounterOperationsAndExport) {
    corium::profiler::Counter counter("events_processed_total", "Total count of processed events");

    EXPECT_EQ(counter.get(), 0u);
    counter.increment();
    counter.increment(4);
    EXPECT_EQ(counter.get(), 5u);

    std::array<char, 256> buffer{};
    size_t len = corium::profiler::formatPrometheusCounter(counter, buffer);
    EXPECT_GT(len, 0u);

    std::string text(buffer.data(), len);
    EXPECT_NE(text.find("# TYPE events_processed_total counter"), std::string::npos);
    EXPECT_NE(text.find("events_processed_total 5"), std::string::npos);

    counter.reset();
    EXPECT_EQ(counter.get(), 0u);
}

TEST(MetricsPrometheusTest, GaugeOperationsAndExport) {
    corium::profiler::Gauge gauge("active_connections", "Number of concurrent connections");

    EXPECT_EQ(gauge.get(), 0);
    gauge.increment(10);
    gauge.decrement(3);
    EXPECT_EQ(gauge.get(), 7);

    gauge.set(-2);
    EXPECT_EQ(gauge.get(), -2);

    std::array<char, 256> buffer{};
    size_t len = corium::profiler::formatPrometheusGauge(gauge, buffer);
    EXPECT_GT(len, 0u);

    std::string text(buffer.data(), len);
    EXPECT_NE(text.find("# TYPE active_connections gauge"), std::string::npos);
    EXPECT_NE(text.find("active_connections -2"), std::string::npos);
}

TEST(MetricsPrometheusTest, HistogramBucketing) {
    std::array<double, 4> bounds = {1.0, 5.0, 10.0, 50.0};
    corium::profiler::Histogram<4> hist("request_latency_ms", bounds, "Request latency histogram");

    EXPECT_EQ(hist.count(), 0u);

    hist.observe(0.5);
    hist.observe(3.2);
    hist.observe(8.0);
    hist.observe(12.0);
    hist.observe(100.0);

    EXPECT_EQ(hist.count(), 5u);

    // <= 1.0: 0.5 (1)
    EXPECT_EQ(hist.bucketCount(0), 1u);
    // <= 5.0: 0.5, 3.2 (2)
    EXPECT_EQ(hist.bucketCount(1), 2u);
    // <= 10.0: 0.5, 3.2, 8.0 (3)
    EXPECT_EQ(hist.bucketCount(2), 3u);
    // <= 50.0: 0.5, 3.2, 8.0, 12.0 (4)
    EXPECT_EQ(hist.bucketCount(3), 4u);
}
