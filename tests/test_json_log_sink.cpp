#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "corium/logging/LogLevel.hpp"
#include "corium/logging/LogEvent.hpp"
#include "corium/logging/sinks/JsonLogSink.hpp"
#include "corium/profiler/ProfilerPolicies.hpp"

TEST(JsonLogSinkTest, FormatsLogEventAsValidJsonLines) {
    std::stringstream ss;
    corium::logging::sinks::JsonLogSink sink(ss);

    corium::logging::LogEventT<128> event;
    event.timestampNs = 1700000000123456789ULL;
    event.level = corium::logging::LogLevel::Info;
    event.category = "NETWORK";
    event.setMessage("Connected to host \"127.0.0.1\":8080\nReady");

    sink.write(event);

    std::string jsonLine = ss.str();
    EXPECT_FALSE(jsonLine.empty());
    EXPECT_NE(jsonLine.find("\"timestamp_ns\":1700000000123456789"), std::string::npos);
    EXPECT_NE(jsonLine.find("\"level\":\"INFO\""), std::string::npos);
    EXPECT_NE(jsonLine.find("\"category\":\"NETWORK\""), std::string::npos);
    EXPECT_NE(jsonLine.find("\\\"127.0.0.1\\\""), std::string::npos); // Escaped quote
    EXPECT_NE(jsonLine.find("\\nReady"), std::string::npos); // Escaped newline
    EXPECT_EQ(jsonLine.back(), '\n');
}

TEST(ProfilerToggleTest, EnableDisableControlsProfiling) {
    corium::profiler::LatencyTracker<128> tracker;
    EXPECT_TRUE(tracker.isEnabled());

    struct DummyEvent { int x; };
    DummyEvent evt{10};

    tracker.onEventPosted(evt, 0);
    EXPECT_EQ(tracker.totalPosted(), 1u);

    tracker.disable();
    EXPECT_FALSE(tracker.isEnabled());

    tracker.onEventPosted(evt, 0);
    EXPECT_EQ(tracker.totalPosted(), 1u); // Unchanged because disabled!

    tracker.enable();
    EXPECT_TRUE(tracker.isEnabled());

    tracker.onEventPosted(evt, 0);
    EXPECT_EQ(tracker.totalPosted(), 2u);
}
