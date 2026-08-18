#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "corium/async/CancellationToken.hpp"
#include "corium/async/Generator.hpp"
#include "corium/async/Task.hpp"
#include "corium/async/WhenAll.hpp"
#include "corium/async/WhenAny.hpp"

namespace {

corium::async::Task<int> asyncCompute(int val) {
    co_return val * 2;
}

corium::async::Task<std::string> asyncMessage(const std::string& msg) {
    co_return "Hello, " + msg;
}

corium::async::Task<void> asyncVoidAction(int& counter) {
    ++counter;
    co_return;
}

corium::async::Generator<int> fibonacciGenerator(int count) {
    int a = 0;
    int b = 1;
    for (int i = 0; i < count; ++i) {
        co_yield a;
        int next = a + b;
        a = b;
        b = next;
    }
}

} // namespace

TEST(AsyncAdvancedTest, CancellationTokenManualCancel) {
    corium::async::CancellationToken token;
    EXPECT_FALSE(token.isCancelled());

    token.cancel();
    EXPECT_TRUE(token.isCancelled());

    token.reset();
    EXPECT_FALSE(token.isCancelled());
}

TEST(AsyncAdvancedTest, WhenAllValueTasks) {
    auto testCoro = []() -> corium::async::Task<bool> {
        auto [num, str] = co_await corium::async::whenAll(
            asyncCompute(21),
            asyncMessage("Corium")
        );

        EXPECT_EQ(num, 42);
        EXPECT_EQ(str, "Hello, Corium");
        co_return true;
    };

    auto task = testCoro();
    task.resume();
    EXPECT_TRUE(task.done());
}

TEST(AsyncAdvancedTest, WhenAllVoidTasks) {
    int counter = 0;
    auto testCoro = [&]() -> corium::async::Task<void> {
        co_await corium::async::whenAll(
            asyncVoidAction(counter),
            asyncVoidAction(counter),
            asyncVoidAction(counter)
        );
    };

    auto task = testCoro();
    task.resume();
    EXPECT_TRUE(task.done());
    EXPECT_EQ(counter, 3);
}

TEST(AsyncAdvancedTest, WhenAnyFastestTask) {
    auto testCoro = []() -> corium::async::Task<bool> {
        auto res = co_await corium::async::whenAny(
            asyncCompute(100),
            asyncCompute(200)
        );

        EXPECT_EQ(res.index, 0u);
        EXPECT_EQ(std::get<0>(res.result), 200);
        co_return true;
    };

    auto task = testCoro();
    task.resume();
    EXPECT_TRUE(task.done());
}

TEST(AsyncAdvancedTest, GeneratorFibonacciSequence) {
    std::vector<int> results;
    for (int val : fibonacciGenerator(7)) {
        results.push_back(val);
    }

    const std::vector<int> expected = {0, 1, 1, 2, 3, 5, 8};
    EXPECT_EQ(results, expected);
}

TEST(AsyncAdvancedTest, GeneratorEarlyBreak) {
    int count = 0;
    for (int val : fibonacciGenerator(100)) {
        (void)val;
        ++count;
        if (count == 5) {
            break;
        }
    }
    EXPECT_EQ(count, 5);
}
