#include <gtest/gtest.h>
#include <corium/internal/MpscRingBuffer.hpp>

#include <atomic>
#include <thread>
#include <vector>

using namespace corium;

TEST(MpscRingBufferTest, BasicPushAndPop) {
    MpscRingBuffer<int, 16> buffer;

    EXPECT_TRUE(buffer.empty());

    auto pushRes = buffer.tryPush(42);
    EXPECT_TRUE(pushRes.pushed);
    EXPECT_TRUE(pushRes.wasEmpty);
    EXPECT_FALSE(buffer.empty());

    int val = 0;
    EXPECT_TRUE(buffer.tryPop(val));
    EXPECT_EQ(val, 42);
    EXPECT_TRUE(buffer.empty());
}

TEST(MpscRingBufferTest, CapacityLimit) {
    MpscRingBuffer<int, 4> buffer;

    EXPECT_TRUE(buffer.tryPush(1).pushed);
    EXPECT_TRUE(buffer.tryPush(2).pushed);
    EXPECT_TRUE(buffer.tryPush(3).pushed);
    EXPECT_TRUE(buffer.tryPush(4).pushed);

    // Buffer is full (capacity = 4)
    EXPECT_FALSE(buffer.tryPush(5).pushed);

    int val = 0;
    EXPECT_TRUE(buffer.tryPop(val));
    EXPECT_EQ(val, 1);

    // Now space is available
    EXPECT_TRUE(buffer.tryPush(5).pushed);
}

TEST(MpscRingBufferTest, MultiProducerConcurrentPush) {
    constexpr size_t BufferCapacity = 1024;
    constexpr int ItemsPerThread = 100;
    constexpr int NumThreads = 4;

    MpscRingBuffer<int, BufferCapacity> buffer;
    std::vector<std::thread> producers;

    for (int t = 0; t < NumThreads; ++t) {
        producers.emplace_back([&buffer, t]() {
            for (int i = 0; i < ItemsPerThread; ++i) {
                while (!buffer.tryPush(t * 1000 + i).pushed) {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& th : producers) {
        th.join();
    }

    int poppedCount = 0;
    int val = 0;
    while (buffer.tryPop(val)) {
        poppedCount++;
    }

    EXPECT_EQ(poppedCount, NumThreads * ItemsPerThread);
    EXPECT_TRUE(buffer.empty());
}
