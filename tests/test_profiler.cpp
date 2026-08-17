#include <gtest/gtest.h>

#include "corium/Application.hpp"
#include "corium/Runtime.hpp"
#include "corium/profiler/FlightRecorder.hpp"
#include "corium/profiler/ProfilerPolicies.hpp"

#include <sstream>
#include <variant>
#include <vector>

using namespace corium;
using namespace corium::profiler;

namespace {

struct SensorReadingEvent {
    int sensorId;
    float value;
};

struct CommandEvent {
    int commandCode;
};

using ProfilerTestEvents = std::variant<QuitEvent, SensorReadingEvent, CommandEvent>;

} // namespace

TEST(ProfilerTest, FlightRecorderBasicRecordingAndOverwrite)
{
    FlightRecorder<4> recorder;
    EXPECT_EQ(recorder.capacity(), 4u);
    EXPECT_EQ(recorder.totalRecorded(), 0u);

    recorder.record(1, "SensorEvent", 1000, 1050, 1100, 0);
    recorder.record(2, "CommandEvent", 2000, 2020, 2040, 1);
    recorder.record(1, "SensorEvent", 3000, 3010, 3030, 0);

    EXPECT_EQ(recorder.totalRecorded(), 3u);

    std::vector<FlightRecord> records;
    recorder.forEach([&](const FlightRecord& r) {
        records.push_back(r);
    });

    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records[0].eventTypeId, 1u);
    EXPECT_EQ(records[1].eventTypeId, 2u);
    EXPECT_EQ(records[2].eventTypeId, 1u);

    // Push 3 more events to test circular overwriting (total 6 records, capacity 4)
    recorder.record(1, "SensorEvent", 4000, 4010, 4020, 0);
    recorder.record(2, "CommandEvent", 5000, 5010, 5030, 1);
    recorder.record(1, "SensorEvent", 6000, 6010, 6040, 0);

    EXPECT_EQ(recorder.totalRecorded(), 6u);

    records.clear();
    recorder.forEach([&](const FlightRecord& r) {
        records.push_back(r);
    });

    ASSERT_EQ(records.size(), 4u); // Should contain the latest 4 records
    EXPECT_EQ(records[0].postTimestampNs, 3000u);
    EXPECT_EQ(records[1].postTimestampNs, 4000u);
    EXPECT_EQ(records[2].postTimestampNs, 5000u);
    EXPECT_EQ(records[3].postTimestampNs, 6000u);
}

TEST(ProfilerTest, FlightRecorderChromeTracingJsonExport)
{
    FlightRecorder<8> recorder;
    recorder.record(1, "SensorEvent", 1000000, 1050000, 1100000, 0);
    recorder.record(2, "CommandEvent", 2000000, 2020000, 2060000, 1);

    std::ostringstream oss;
    recorder.exportChromeTracingJson(oss);
    const std::string json = oss.str();

    EXPECT_NE(json.find("\"name\": \"QueueWait[SensorEvent]\""), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"Dispatch[SensorEvent]\""), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"QueueWait[CommandEvent]\""), std::string::npos);
    EXPECT_NE(json.find("\"name\": \"Dispatch[CommandEvent]\""), std::string::npos);
    EXPECT_NE(json.find("\"cat\": \"corium\""), std::string::npos);
}

TEST(ProfilerTest, LatencyTrackerStatistics)
{
    LatencyTracker tracker;
    EXPECT_EQ(tracker.totalPosted(), 0u);
    EXPECT_EQ(tracker.totalDispatched(), 0u);

    ProfilerTestEvents ev{SensorReadingEvent{1, 42.0f}};
    tracker.onEventPosted(ev, 0);
    tracker.onEventDispatched(ev, 0, 1000, 2000, 5000); // queue Latency = 1us, exec = 3us

    EXPECT_EQ(tracker.totalPosted(), 1u);
    EXPECT_EQ(tracker.totalDispatched(), 1u);
    EXPECT_DOUBLE_EQ(tracker.minQueueLatencyUs(), 1.0);
    EXPECT_DOUBLE_EQ(tracker.maxQueueLatencyUs(), 1.0);
    EXPECT_DOUBLE_EQ(tracker.averageQueueLatencyUs(), 1.0);
    EXPECT_DOUBLE_EQ(tracker.maxExecutionDurationUs(), 3.0);
    EXPECT_DOUBLE_EQ(tracker.averageExecutionDurationUs(), 3.0);

    tracker.resetStats();
    EXPECT_EQ(tracker.totalPosted(), 0u);
    EXPECT_EQ(tracker.totalDispatched(), 0u);
}

TEST(ProfilerTest, RuntimeIntegrationWithFlightRecorder)
{
    using ProfiledRuntime = RuntimeBuilder<>
        ::WithEvents<ProfilerTestEvents>
        ::WithFlightRecorder<128>
        ::Build;

    class App : public Application<App, ProfiledRuntime::EventBusType> {
    public:
        int sensorCount = 0;
        int commandCount = 0;

        void onRegisterHandlers() {
            this->on([this](const SensorReadingEvent&) {
                sensorCount++;
            });
            this->on([this](const CommandEvent&) {
                commandCount++;
            });
        }
    };

    ProfiledRuntime runtime;
    App app;
    runtime.initialize(app);

    auto sink = runtime.eventSink();
    for (int i = 0; i < 10; ++i) {
        sink.post(SensorReadingEvent{i, static_cast<float>(i) * 1.5f});
    }
    sink.post(CommandEvent{99});

    runtime.pump();

    EXPECT_EQ(app.sensorCount, 10);
    EXPECT_EQ(app.commandCount, 1);

    // Verify profiler stats
    EXPECT_EQ(runtime.profiler().totalPosted(), 11u);
    EXPECT_EQ(runtime.profiler().totalDispatched(), 11u);
    EXPECT_GE(runtime.profiler().maxExecutionDurationUs(), 0.0);
    EXPECT_EQ(runtime.profiler().flightRecorder().totalRecorded(), 11u);

    std::ostringstream jsonOut;
    runtime.profiler().exportChromeTracingJson(jsonOut);
    EXPECT_FALSE(jsonOut.str().empty());

    runtime.shutdown();
}
