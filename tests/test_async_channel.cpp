#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

#include "corium/corium.hpp"

namespace {

corium::async::Task<void> producerTask(corium::async::Channel<int, 4>& chan) {
    for (int i = 1; i <= 5; ++i) {
        co_await chan.push(i * 10);
    }
    chan.close();
}

corium::async::Task<int> consumerTask(corium::async::Channel<int, 4>& chan, std::vector<int>& output) {
    int sum = 0;
    while (true) {
        auto opt = co_await chan.pop();
        if (!opt.has_value()) {
            break;
        }
        output.push_back(*opt);
        sum += *opt;
    }
    co_return sum;
}

corium::async::Task<void> semaphoreWorker(corium::async::AsyncSemaphore& sem, int& activeCount, int& maxObserved) {
    co_await sem.acquire();
    activeCount++;
    if (activeCount > maxObserved) {
        maxObserved = activeCount;
    }
    activeCount--;
    sem.release();
}

} // namespace

TEST(AsyncChannelTest, SynchronousTryPushAndTryPop) {
    corium::async::Channel<int, 3> chan;

    EXPECT_TRUE(chan.empty());
    EXPECT_FALSE(chan.full());
    EXPECT_EQ(chan.size(), 0u);

    EXPECT_TRUE(chan.tryPush(10));
    EXPECT_TRUE(chan.tryPush(20));
    EXPECT_TRUE(chan.tryPush(30));
    EXPECT_FALSE(chan.tryPush(40)); // Full

    EXPECT_TRUE(chan.full());
    EXPECT_EQ(chan.size(), 3u);

    int out = 0;
    EXPECT_TRUE(chan.tryPop(out));
    EXPECT_EQ(out, 10);
    EXPECT_TRUE(chan.tryPop(out));
    EXPECT_EQ(out, 20);
    EXPECT_TRUE(chan.tryPop(out));
    EXPECT_EQ(out, 30);
    EXPECT_FALSE(chan.tryPop(out)); // Empty
}

TEST(AsyncChannelTest, CoroutineProducerConsumerMessagePassing) {
    corium::async::Channel<int, 4> chan;
    std::vector<int> received;

    auto prod = producerTask(chan);
    auto cons = consumerTask(chan, received);

    // Initial resumption
    prod.resume();
    cons.resume();

    EXPECT_TRUE(chan.isClosed());
    ASSERT_EQ(received.size(), 5u);
    EXPECT_EQ(received[0], 10);
    EXPECT_EQ(received[1], 20);
    EXPECT_EQ(received[2], 30);
    EXPECT_EQ(received[3], 40);
    EXPECT_EQ(received[4], 50);
}

TEST(AsyncChannelTest, AsyncSemaphoreAcquireAndRelease) {
    corium::async::AsyncSemaphore sem(2); // 2 permits

    int active = 0;
    int maxObs = 0;

    auto w1 = semaphoreWorker(sem, active, maxObs);
    auto w2 = semaphoreWorker(sem, active, maxObs);
    auto w3 = semaphoreWorker(sem, active, maxObs);

    w1.resume();
    w2.resume();
    w3.resume();

    EXPECT_LE(maxObs, 2);
    EXPECT_EQ(sem.available(), 2);
}
