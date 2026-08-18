#include <gtest/gtest.h>
#include <cstdint>
#include <variant>
#include <vector>

#include "corium/corium.hpp"

namespace {

struct TelemetryEvent {
    uint32_t sensorId;
    float value;
};

struct CommandEvent {
    uint16_t commandCode;
    uint32_t param;
};

using JournalTestEvents = std::variant<
    corium::QuitEvent,
    TelemetryEvent,
    CommandEvent
>;

using JournalTestRuntime = corium::RuntimeBuilder
    ::WithEvents<JournalTestEvents>
    ::WithCapacity<64>
    ::Build;

class JournalTestApp : public corium::Application<JournalTestApp, JournalTestEvents> {
public:
    std::vector<TelemetryEvent> receivedTelemetry;
    std::vector<CommandEvent> receivedCommands;

    void onRegisterHandlers() {
        on([this](const TelemetryEvent& e) {
            receivedTelemetry.push_back(e);
        });
        on([this](const CommandEvent& e) {
            receivedCommands.push_back(e);
        });
    }
};

} // namespace

TEST(EventJournalTest, BasicRecordingAndReplay) {
    corium::wire::EventJournalWriter<JournalTestEvents, 1024> writer;

    EXPECT_EQ(writer.recordCount(), 0u);
    EXPECT_GT(writer.bytesWritten(), 0u); // Header size

    // Record events with timestamps
    EXPECT_TRUE(writer.record(TelemetryEvent{.sensorId = 1, .value = 24.5f}, 1000));
    EXPECT_TRUE(writer.record(TelemetryEvent{.sensorId = 2, .value = 98.2f}, 1500));
    EXPECT_TRUE(writer.record(CommandEvent{.commandCode = 0x42, .param = 1337}, 2000));

    EXPECT_EQ(writer.recordCount(), 3u);

    // Read and replay into runtime
    corium::wire::EventJournalReader<JournalTestEvents> reader(writer.data());
    EXPECT_TRUE(reader.isValid());
    EXPECT_EQ(reader.totalRecords(), 3u);

    JournalTestRuntime runtime;
    JournalTestApp app;
    runtime.initialize(app);

    auto sink = runtime.eventSink();
    size_t replayed = reader.replayInto(sink);
    EXPECT_EQ(replayed, 3u);

    runtime.drain();

    ASSERT_EQ(app.receivedTelemetry.size(), 2u);
    EXPECT_EQ(app.receivedTelemetry[0].sensorId, 1u);
    EXPECT_FLOAT_EQ(app.receivedTelemetry[0].value, 24.5f);
    EXPECT_EQ(app.receivedTelemetry[1].sensorId, 2u);
    EXPECT_FLOAT_EQ(app.receivedTelemetry[1].value, 98.2f);

    ASSERT_EQ(app.receivedCommands.size(), 1u);
    EXPECT_EQ(app.receivedCommands[0].commandCode, 0x42);
    EXPECT_EQ(app.receivedCommands[0].param, 1337u);

    runtime.shutdown();
}

TEST(EventJournalTest, BufferOverflowProtection) {
    // Very small buffer that fits header + ~1 record
    corium::wire::EventJournalWriter<JournalTestEvents, sizeof(corium::wire::JournalHeader) + sizeof(corium::wire::JournalRecordHeader) + sizeof(TelemetryEvent) + 4> smallWriter;

    EXPECT_TRUE(smallWriter.record(TelemetryEvent{.sensorId = 1, .value = 1.0f}, 100));
    // Second write should safely fail without crashing
    EXPECT_FALSE(smallWriter.record(TelemetryEvent{.sensorId = 2, .value = 2.0f}, 200));
    EXPECT_EQ(smallWriter.recordCount(), 1u);
}

TEST(EventJournalTest, CorruptedRecordCrcRejection) {
    corium::wire::EventJournalWriter<JournalTestEvents, 1024> writer;
    writer.record(TelemetryEvent{.sensorId = 10, .value = 3.14f}, 100);
    writer.record(TelemetryEvent{.sensorId = 20, .value = 6.28f}, 200);

    auto dataSpan = writer.data();
    std::vector<uint8_t> corruptedData(dataSpan.begin(), dataSpan.end());

    // Corrupt one byte of payload in the first record
    size_t payloadOffset = sizeof(corium::wire::JournalHeader) + sizeof(corium::wire::JournalRecordHeader);
    corruptedData[payloadOffset] ^= 0xFF;

    corium::wire::EventJournalReader<JournalTestEvents> reader(corruptedData);
    EXPECT_TRUE(reader.isValid());

    JournalTestRuntime runtime;
    JournalTestApp app;
    runtime.initialize(app);
    auto sink = runtime.eventSink();

    // Replay should abort on corrupted record CRC and replay 0 records
    size_t replayed = reader.replayInto(sink);
    EXPECT_EQ(replayed, 0u);

    runtime.shutdown();
}
