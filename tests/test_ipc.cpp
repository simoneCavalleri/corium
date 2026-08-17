#include <gtest/gtest.h>
#include <variant>
#include "corium/Application.hpp"
#include "corium/Runtime.hpp"
#include "corium/ipc/IpcChannel.hpp"
#include "corium/ipc/SharedMemory.hpp"
#include "corium/ipc/ShmMpscQueue.hpp"
#include "corium/ipc/UdsChannel.hpp"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

using namespace corium;
using namespace corium::ipc;

namespace {

struct IpcTelemetryEvent {
    uint32_t sensorId;
    float value;
};

struct IpcCommandEvent {
    uint32_t commandId;
    uint32_t payload;
};

using IpcTestEvents = std::variant<
    QuitEvent,
    IpcTelemetryEvent,
    IpcCommandEvent
>;

} // namespace

TEST(IpcTest, SharedMemoryCreateAndMap)
{
    const std::string shmName = "/corium_test_shm_basic";
    SharedMemory::unlink(shmName);

    SharedMemory shm;
    EXPECT_TRUE(shm.open(shmName, 1024, SharedMemory::AccessMode::CreateOrOpen));
    EXPECT_TRUE(shm.isValid());
    EXPECT_GE(shm.size(), 1024u);
    EXPECT_NE(shm.data(), nullptr);

    // Write to shared memory
    int* ptr = static_cast<int*>(shm.data());
    *ptr = 42;

    // Attach second instance
    SharedMemory clientShm;
    EXPECT_TRUE(clientShm.open(shmName, 1024, SharedMemory::AccessMode::OpenReadWrite));
    EXPECT_TRUE(clientShm.isValid());
    int* clientPtr = static_cast<int*>(clientShm.data());
    EXPECT_EQ(*clientPtr, 42);

    shm.close();
    clientShm.close();
    SharedMemory::unlink(shmName);
}

TEST(IpcTest, ShmMpscQueuePushPop)
{
    alignas(64) uint8_t buffer[ShmMpscQueue<int, 4>::requiredMemorySize()]{};
    ShmMpscQueue<int, 4> queue(buffer, true);

    EXPECT_TRUE(queue.isValid());
    EXPECT_TRUE(queue.empty());

    // Push 4 elements
    EXPECT_TRUE(queue.tryPush(10));
    EXPECT_TRUE(queue.tryPush(20));
    EXPECT_TRUE(queue.tryPush(30));
    EXPECT_TRUE(queue.tryPush(40));

    // 5th push should fail (full)
    EXPECT_FALSE(queue.tryPush(50));

    // Pop elements in order
    int val = 0;
    EXPECT_TRUE(queue.tryPop(val));
    EXPECT_EQ(val, 10);
    EXPECT_TRUE(queue.tryPop(val));
    EXPECT_EQ(val, 20);

    // Push 2 more (circular wrap-around)
    EXPECT_TRUE(queue.tryPush(50));
    EXPECT_TRUE(queue.tryPush(60));

    EXPECT_TRUE(queue.tryPop(val));
    EXPECT_EQ(val, 30);
    EXPECT_TRUE(queue.tryPop(val));
    EXPECT_EQ(val, 40);
    EXPECT_TRUE(queue.tryPop(val));
    EXPECT_EQ(val, 50);
    EXPECT_TRUE(queue.tryPop(val));
    EXPECT_EQ(val, 60);

    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.tryPop(val));
}

TEST(IpcTest, IpcChannelTypedEventExchange)
{
    const std::string channelName = "/corium_ipc_test_channel";
    SharedMemory::unlink(channelName);

    IpcChannel<IpcTestEvents, 16> hostChannel;
    EXPECT_TRUE(hostChannel.create(channelName));
    EXPECT_TRUE(hostChannel.isValid());

    IpcChannel<IpcTestEvents, 16> clientChannel;
    EXPECT_TRUE(clientChannel.attach(channelName));
    EXPECT_TRUE(clientChannel.isValid());

    // Client posts events into shared memory
    EXPECT_TRUE(clientChannel.post(IpcTelemetryEvent{1, 98.6f}));
    EXPECT_TRUE(clientChannel.post(IpcCommandEvent{42, 100}));

    // Host receives and drains into a Corium Runtime
    using IpcRuntime = RuntimeBuilder<>
        ::WithEvents<IpcTestEvents>
        ::Build;

    class ReceiverApp : public Application<ReceiverApp, IpcRuntime::EventBusType> {
    public:
        int telemetryReceived = 0;
        int commandsReceived = 0;
        float lastVal = 0.0f;
        uint32_t lastCmd = 0;

        void onRegisterHandlers()
        {
            this->on([this](const IpcTelemetryEvent& e) {
                telemetryReceived++;
                lastVal = e.value;
            });
            this->on([this](const IpcCommandEvent& e) {
                commandsReceived++;
                lastCmd = e.commandId;
            });
        }
    };

    IpcRuntime runtime;
    ReceiverApp app;
    runtime.initialize(app);

    std::size_t pumped = hostChannel.pumpInto(runtime.eventSink());
    EXPECT_EQ(pumped, 2u);

    runtime.pump();

    EXPECT_EQ(app.telemetryReceived, 1);
    EXPECT_FLOAT_EQ(app.lastVal, 98.6f);
    EXPECT_EQ(app.commandsReceived, 1);
    EXPECT_EQ(app.lastCmd, 42u);

    runtime.shutdown();
    hostChannel.unlink();
}

TEST(IpcTest, ConcurrentMultiProducerPush)
{
    const std::string channelName = "/corium_ipc_test_concurrent";
    SharedMemory::unlink(channelName);

    IpcChannel<IpcTestEvents, 1024> hostChannel;
    ASSERT_TRUE(hostChannel.create(channelName));

    constexpr int numThreads = 4;
    constexpr int itemsPerThread = 100;
    std::vector<std::thread> producers;

    for (int t = 0; t < numThreads; ++t) {
        producers.emplace_back([t, &channelName]() {
            IpcChannel<IpcTestEvents, 1024> clientChannel;
            ASSERT_TRUE(clientChannel.attach(channelName));

            for (int i = 0; i < itemsPerThread; ++i) {
                while (!clientChannel.post(IpcTelemetryEvent{static_cast<uint32_t>(t), static_cast<float>(i)})) {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& th : producers) {
        th.join();
    }

    // Drain all pushed events
    int totalPopped = 0;
    IpcTestEvents ev;
    while (hostChannel.tryPop(ev)) {
        totalPopped++;
    }

    EXPECT_EQ(totalPopped, numThreads * itemsPerThread);
    hostChannel.unlink();
}

TEST(IpcTest, UdsChannelDatagramEventTransmission)
{
    const std::string serverSock = "/tmp/corium_test_uds_srv.sock";
    const std::string clientSock = "/tmp/corium_test_uds_cli.sock";

    UdsChannel<IpcTestEvents> server;
    ASSERT_TRUE(server.listen(serverSock, true));
    EXPECT_TRUE(server.isOpen());

    UdsChannel<IpcTestEvents> client;
    ASSERT_TRUE(client.connect(serverSock, clientSock));
    EXPECT_TRUE(client.isOpen());

    // Client posts event over UNIX domain socket datagram
    EXPECT_TRUE(client.post(IpcCommandEvent{999, 1234}));

    // Server pops non-blocking datagram
    IpcTestEvents received;
    EXPECT_TRUE(server.tryPop(received));

    bool handled = false;
    std::visit([&](auto&& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, IpcCommandEvent>) {
            EXPECT_EQ(e.commandId, 999u);
            EXPECT_EQ(e.payload, 1234u);
            handled = true;
        }
    }, received);
    EXPECT_TRUE(handled);

    server.close();
    client.close();
}

