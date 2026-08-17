#include <gtest/gtest.h>
#include <corium/corium.hpp>
#include <vector>

using namespace corium;
using namespace corium::async;

Task<int> asyncCompute(int a, int b)
{
    co_await yield();
    co_return a + b;
}

Task<int> asyncChained()
{
    int r1 = co_await asyncCompute(10, 20);
    int r2 = co_await asyncCompute(r1, 30);
    co_return r2;
}

Task<void> asyncSequence(std::vector<std::string>& log)
{
    log.push_back("Step 1");
    co_await yield();
    log.push_back("Step 2");
    co_await yield();
    log.push_back("Step 3");
}

TEST(AsyncTaskTest, SimpleTaskComputation)
{
    auto task = asyncCompute(5, 7);
    EXPECT_FALSE(task.done());
    task.resume();
    EXPECT_TRUE(task.done());
}

TEST(AsyncTaskTest, ChainedTaskAwait)
{
    auto task = asyncChained();
    EXPECT_FALSE(task.done());
    task.resume();
    EXPECT_TRUE(task.done());
}

TEST(AsyncTaskTest, VoidTaskSequence)
{
    std::vector<std::string> log;
    auto task = asyncSequence(log);
    EXPECT_FALSE(task.done());
    EXPECT_TRUE(log.empty());

    task.resume();
    EXPECT_TRUE(task.done());
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], "Step 1");
    EXPECT_EQ(log[1], "Step 2");
    EXPECT_EQ(log[2], "Step 3");
}

TEST(AsyncTaskTest, DelayAwaitable)
{
    auto start = std::chrono::steady_clock::now();

    auto runDelay = []() -> Task<void> {
        co_await delay(std::chrono::milliseconds(20));
    };

    auto task = runDelay();
    task.resume();
    EXPECT_TRUE(task.done());

    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_GE(elapsed, std::chrono::milliseconds(15));
}
