#include <gtest/gtest.h>
#include <cstdint>
#include <chrono>
#include <thread>
#include <variant>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using test_socklen_t = int;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using test_socklen_t = socklen_t;
#endif

#include "corium/corium.hpp"

namespace {

struct NetSensorEvent {
    uint32_t sensorId;
    float temperature;
    float humidity;
};

using NetTestEvents = std::variant<
    corium::QuitEvent,
    NetSensorEvent
>;

using NetTestRuntime = corium::RuntimeBuilder
    ::WithEvents<NetTestEvents>
    ::WithCapacity<64>
    ::Build;

class NetTestApp : public corium::Application<NetTestApp, NetTestEvents> {
public:
    std::vector<NetSensorEvent> received;

    void onRegisterHandlers() {
        on([this](const NetSensorEvent& e) {
            received.push_back(e);
        });
    }
};

} // namespace

TEST(StaticUdpChannelTest, OpenBindSendAndReceiveEvent) {
    corium::net::StaticUdpChannel<256> serverChannel;
    corium::net::StaticUdpChannel<256> clientChannel;

    // Bind receiver to loopback on ephemeral port
    ASSERT_TRUE(serverChannel.openAndBind(0, "127.0.0.1"));
    ASSERT_TRUE(serverChannel.setNonBlocking(true));

    // Get bound port
    sockaddr_in sin{};
    test_socklen_t len = static_cast<test_socklen_t>(sizeof(sin));
#if defined(_WIN32) || defined(_WIN64)
    ASSERT_EQ(getsockname(static_cast<SOCKET>(serverChannel.nativeHandle()), reinterpret_cast<sockaddr*>(&sin), &len), 0);
#else
    ASSERT_EQ(getsockname(serverChannel.nativeHandle(), reinterpret_cast<sockaddr*>(&sin), &len), 0);
#endif
    uint16_t serverPort = ntohs(sin.sin_port);

    // Send typed event via clientChannel
    NetSensorEvent evt{.sensorId = 42, .temperature = 23.5f, .humidity = 55.0f};
    bool sent = clientChannel.sendEvent<NetSensorEvent, NetTestEvents>("127.0.0.1", serverPort, evt);
    EXPECT_TRUE(sent);

    // Setup runtime and app
    NetTestRuntime runtime;
    NetTestApp app;
    runtime.initialize(app);
    auto sink = runtime.eventSink();

    // Receive and push into sink
    bool pushed = false;
    for (int retry = 0; retry < 100; ++retry) {
        if (serverChannel.receiveAndPush<NetTestEvents>(sink)) {
            pushed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(pushed);

    runtime.drain();

    ASSERT_EQ(app.received.size(), 1u);
    EXPECT_EQ(app.received[0].sensorId, 42u);
    EXPECT_FLOAT_EQ(app.received[0].temperature, 23.5f);
    EXPECT_FLOAT_EQ(app.received[0].humidity, 55.0f);

    serverChannel.close();
    clientChannel.close();
    runtime.shutdown();
}
