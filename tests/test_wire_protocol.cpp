#include <gtest/gtest.h>
#include <corium/corium.hpp>
#include <vector>

using namespace corium;
using namespace corium::wire;

struct WireSensorData {
    uint32_t sensorId;
    float temperature;
    float humidity;
};

struct WireAlertEvent {
    uint16_t alertCode;
    uint8_t severity;
};

using WireTestEvents = std::variant<QuitEvent, WireSensorData, WireAlertEvent>;

TEST(WireProtocolTest, SerializeAndValidatePacket)
{
    WireSensorData data{101, 24.5f, 55.2f};
    auto packet = WireSerializer::serialize<WireSensorData, WireTestEvents>(data);

    EXPECT_TRUE(packet.isValid());
    EXPECT_EQ(packet.header.magic, CORIUM_WIRE_MAGIC);
    EXPECT_EQ(packet.header.payloadLength, sizeof(WireSensorData));
    EXPECT_EQ(packet.header.typeIndex, 1); // WireSensorData is at index 1

    // Tamper with payload -> CRC check must fail
    packet.payload[0] ^= 0xFF;
    EXPECT_FALSE(packet.isValid());
}

TEST(WireProtocolTest, DeserializeDirectlyIntoRuntime)
{
    using WireRuntime = RuntimeBuilder<>::WithEvents<WireTestEvents>::Build;

    class App : public Application<App, WireRuntime::EventBusType> {
    public:
        std::vector<uint32_t> sensors;
        std::vector<uint16_t> alerts;

        void onRegisterHandlers() {
            this->on([this](const WireSensorData& s) {
                sensors.push_back(s.sensorId);
            });
            this->on([this](const WireAlertEvent& a) {
                alerts.push_back(a.alertCode);
            });
        }
    };

    WireRuntime runtime;
    App app;
    runtime.initialize(app);

    auto sink = runtime.eventSink();

    // 1. Serialize and deserialize WireSensorData
    WireSensorData s1{404, 31.0f, 60.0f};
    auto p1 = WireSerializer::serialize<WireSensorData, WireTestEvents>(s1);
    EXPECT_TRUE(WireSerializer::deserializeAndPush<WireTestEvents>(p1, sink));

    // 2. Serialize and deserialize WireAlertEvent
    WireAlertEvent a1{999, 3};
    auto p2 = WireSerializer::serialize<WireAlertEvent, WireTestEvents>(a1);
    EXPECT_TRUE(WireSerializer::deserializeAndPush<WireTestEvents>(p2, sink));

    runtime.pump();

    ASSERT_EQ(app.sensors.size(), 1u);
    EXPECT_EQ(app.sensors[0], 404u);

    ASSERT_EQ(app.alerts.size(), 1u);
    EXPECT_EQ(app.alerts[0], 999u);

    // 3. Deserializing corrupted packet fails and is discarded safely
    p1.payload[1] ^= 0xAA;
    EXPECT_FALSE(WireSerializer::deserializeAndPush<WireTestEvents>(p1, sink));

    runtime.shutdown();
}
