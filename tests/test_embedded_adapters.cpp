#include <gtest/gtest.h>
#include <corium/corium.hpp>
#include <vector>

using namespace corium;
using namespace corium::embedded;

struct CustomCanMsg {
    uint32_t id;
    uint8_t payload[8];

    explicit CustomCanMsg(const CanFrame& f) : id(f.id) {
        std::memcpy(payload, f.data.data(), 8);
    }
};

struct UartLineEvent {
    std::vector<uint8_t> raw;

    explicit UartLineEvent(std::span<const uint8_t> span)
        : raw(span.begin(), span.end())
    {}
};

using EmbeddedTestEvents = std::variant<
    QuitEvent,
    CanFrame,
    CanFdFrame,
    CustomCanMsg,
    UartLineEvent
>;

struct MockSink {
    std::vector<EmbeddedTestEvents> events;
    std::vector<EventPriority> priorities;

    void post(EmbeddedTestEvents evt, EventPriority priority = EventPriority::Normal) {
        events.push_back(std::move(evt));
        priorities.push_back(priority);
    }
};

TEST(EmbeddedAdaptersTest, CanClassicAndFdIsrPost)
{
    MockSink mock;
    EventSinkT<EmbeddedTestEvents> sink(mock);

    CanIsrAdapter<EmbeddedTestEvents> adapter(sink);

    // 1. Post standard CAN frame
    CanFrame classicFrame{
        .id = 0x123,
        .dlc = 4,
        .isExtended = false,
        .isRtr = false,
        .data = {0xDE, 0xAD, 0xBE, 0xEF}
    };
    adapter.postFromIsr(classicFrame, EventPriority::Normal);

    // 2. Post CAN-FD frame with High Priority
    CanFdFrame fdFrame{
        .id = 0x18FF0001,
        .len = 16,
        .isExtended = true,
        .bitRateSwitch = true,
        .errorStateIndicator = false,
        .data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}
    };
    adapter.postFromIsr(fdFrame, EventPriority::High);

    // 3. Post into custom typed CAN event
    adapter.postFromIsr<CustomCanMsg>(classicFrame);

    ASSERT_EQ(mock.events.size(), 3u);

    // Verify events and priorities
    EXPECT_TRUE(std::holds_alternative<CanFrame>(mock.events[0]));
    EXPECT_EQ(std::get<CanFrame>(mock.events[0]).id, 0x123u);
    EXPECT_EQ(mock.priorities[0], EventPriority::Normal);

    EXPECT_TRUE(std::holds_alternative<CanFdFrame>(mock.events[1]));
    EXPECT_EQ(std::get<CanFdFrame>(mock.events[1]).id, 0x18FF0001u);
    EXPECT_EQ(mock.priorities[1], EventPriority::High);

    EXPECT_TRUE(std::holds_alternative<CustomCanMsg>(mock.events[2]));
    EXPECT_EQ(std::get<CustomCanMsg>(mock.events[2]).id, 0x123u);
}

TEST(EmbeddedAdaptersTest, DmaUartCircularBufferAndWrapping)
{
    MockSink mock;
    EventSinkT<EmbeddedTestEvents> sink(mock);

    DmaUartAdapter<EmbeddedTestEvents, 16> adapter(sink);
    auto* buf = adapter.rxBuffer().dmaBuffer();

    // 1. Fill first 6 bytes [0..5]
    for (uint8_t i = 0; i < 6; ++i) {
        buf[i] = 0xA0 + i;
    }
    adapter.processFromIsr<UartLineEvent>(6);

    ASSERT_EQ(mock.events.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<UartLineEvent>(mock.events[0]));
    EXPECT_EQ(std::get<UartLineEvent>(mock.events[0]).raw.size(), 6u);
    EXPECT_EQ(std::get<UartLineEvent>(mock.events[0]).raw[0], 0xA0);

    // 2. Fill up to 14 bytes [6..13]
    for (uint8_t i = 6; i < 14; ++i) {
        buf[i] = 0xA0 + i;
    }
    adapter.processFromIsr<UartLineEvent>(14);

    ASSERT_EQ(mock.events.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<UartLineEvent>(mock.events[1]));
    EXPECT_EQ(std::get<UartLineEvent>(mock.events[1]).raw.size(), 8u);

    // 3. Wrap around: fill [14, 15] and [0, 1, 2] (current write index = 3)
    buf[14] = 0xFE;
    buf[15] = 0xFF;
    buf[0] = 0x10;
    buf[1] = 0x11;
    buf[2] = 0x12;

    adapter.processFromIsr<UartLineEvent>(3);

    // Should receive two slices (wrap-around tail and wrapped head)
    ASSERT_EQ(mock.events.size(), 4u);
    ASSERT_TRUE(std::holds_alternative<UartLineEvent>(mock.events[2]));
    EXPECT_EQ(std::get<UartLineEvent>(mock.events[2]).raw.size(), 2u);
    EXPECT_EQ(std::get<UartLineEvent>(mock.events[2]).raw[0], 0xFE);
    EXPECT_EQ(std::get<UartLineEvent>(mock.events[2]).raw[1], 0xFF);

    ASSERT_TRUE(std::holds_alternative<UartLineEvent>(mock.events[3]));
    EXPECT_EQ(std::get<UartLineEvent>(mock.events[3]).raw.size(), 3u);
    EXPECT_EQ(std::get<UartLineEvent>(mock.events[3]).raw[0], 0x10);
    EXPECT_EQ(std::get<UartLineEvent>(mock.events[3]).raw[2], 0x12);
}
