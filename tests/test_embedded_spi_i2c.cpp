#include <gtest/gtest.h>
#include <cstdint>
#include <variant>
#include <vector>

#include "corium/corium.hpp"

namespace {

using SpiI2cTestEvents = std::variant<
    corium::QuitEvent,
    corium::embedded::SpiFrame<32>,
    corium::embedded::I2cFrame<32>
>;

using SpiI2cTestRuntime = corium::RuntimeBuilder
    ::WithEvents<SpiI2cTestEvents>
    ::WithCapacity<64>
    ::Build;

class SpiI2cTestApp : public corium::Application<SpiI2cTestApp, SpiI2cTestEvents> {
public:
    std::vector<corium::embedded::SpiFrame<32>> receivedSpi;
    std::vector<corium::embedded::I2cFrame<32>> receivedI2c;

    void onRegisterHandlers() {
        on([this](const corium::embedded::SpiFrame<32>& e) {
            receivedSpi.push_back(e);
        });
        on([this](const corium::embedded::I2cFrame<32>& e) {
            receivedI2c.push_back(e);
        });
    }
};

} // namespace

TEST(EmbeddedSpiI2cTest, SpiIsrAdapterDispatch) {
    SpiI2cTestRuntime runtime;
    SpiI2cTestApp app;
    runtime.initialize(app);

    auto sink = runtime.eventSink();
    corium::embedded::SpiIsrAdapter<SpiI2cTestEvents> spiAdapter(sink);

    // 1. Post concrete SpiFrame from ISR
    corium::embedded::SpiFrame<32> frame1{};
    frame1.chipSelectId = 1;
    frame1.length = 4;
    frame1.rxData[0] = 0xAA;
    frame1.rxData[1] = 0xBB;
    frame1.rxData[2] = 0xCC;
    frame1.rxData[3] = 0xDD;
    spiAdapter.postFromIsr(frame1);

    // 2. Post raw DMA byte buffer from ISR
    const uint8_t rawDma[3] = {0x11, 0x22, 0x33};
    spiAdapter.postBufferFromIsr(2, std::span<const uint8_t>(rawDma, 3));

    runtime.drain();

    ASSERT_EQ(app.receivedSpi.size(), 2u);
    EXPECT_EQ(app.receivedSpi[0].chipSelectId, 1u);
    EXPECT_EQ(app.receivedSpi[0].length, 4u);
    EXPECT_EQ(app.receivedSpi[0].rxData[0], 0xAA);

    EXPECT_EQ(app.receivedSpi[1].chipSelectId, 2u);
    EXPECT_EQ(app.receivedSpi[1].length, 3u);
    EXPECT_EQ(app.receivedSpi[1].rxData[0], 0x11);

    runtime.shutdown();
}

TEST(EmbeddedSpiI2cTest, I2cIsrAdapterDispatch) {
    SpiI2cTestRuntime runtime;
    SpiI2cTestApp app;
    runtime.initialize(app);

    auto sink = runtime.eventSink();
    corium::embedded::I2cIsrAdapter<SpiI2cTestEvents> i2cAdapter(sink);

    // 1. Post concrete I2cFrame from ISR
    corium::embedded::I2cFrame<32> frame1{};
    frame1.deviceAddress = 0x68; // MPU6050 IMU address
    frame1.registerAddress = 0x3B; // ACCEL_XOUT_H
    frame1.opType = corium::embedded::I2cOpType::Read;
    frame1.length = 6;
    frame1.data[0] = 0x01;
    frame1.data[1] = 0x02;
    i2cAdapter.postFromIsr(frame1);

    // 2. Post Read bytes helper
    const uint8_t regVal[2] = {0x00, 0x07};
    i2cAdapter.postReadFromIsr(0x76, 0xD0, std::span<const uint8_t>(regVal, 2));

    runtime.drain();

    ASSERT_EQ(app.receivedI2c.size(), 2u);
    EXPECT_EQ(app.receivedI2c[0].deviceAddress, 0x68);
    EXPECT_EQ(app.receivedI2c[0].registerAddress, 0x3B);
    EXPECT_EQ(app.receivedI2c[0].length, 6u);

    EXPECT_EQ(app.receivedI2c[1].deviceAddress, 0x76);
    EXPECT_EQ(app.receivedI2c[1].registerAddress, 0xD0);
    EXPECT_EQ(app.receivedI2c[1].length, 2u);
    EXPECT_EQ(app.receivedI2c[1].data[1], 0x07);

    runtime.shutdown();
}
