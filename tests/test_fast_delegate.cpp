#include <gtest/gtest.h>
#include <corium/internal/FastDelegate.hpp>
#include <corium/Events.hpp>

using namespace corium;

struct DestructorTracker {
    static int destroyCount;
    int value = 0;

    DestructorTracker(int v) : value(v) {}
    ~DestructorTracker() { destroyCount++; }

    DestructorTracker(const DestructorTracker&) = default;
    DestructorTracker(DestructorTracker&&) noexcept = default;
    DestructorTracker& operator=(const DestructorTracker&) = default;
    DestructorTracker& operator=(DestructorTracker&&) noexcept = default;
};

int DestructorTracker::destroyCount = 0;

TEST(FastDelegateTest, InlineSBOExecution) {
    int invokedVal = 0;
    EventHandlerDelegate<TickEvent> delegate([&invokedVal](const TickEvent& e) {
        invokedVal = static_cast<int>(e.deltaTime * 100);
    });

    EXPECT_TRUE(static_cast<bool>(delegate));

    delegate.invoke(TickEvent{0.42});
    EXPECT_EQ(invokedVal, 42);
}

TEST(FastDelegateTest, LargeHeapFallbackExecution) {
    // Large capture exceeding 32 bytes to force heap fallback
    char largeBuffer[128];
    for (int i = 0; i < 128; ++i) largeBuffer[i] = static_cast<char>(i);

    int sum = 0;
    EventHandlerDelegate<TickEvent> delegate([largeBuffer, &sum](const TickEvent&) {
        for (int i = 0; i < 128; ++i) sum += largeBuffer[i];
    });

    EXPECT_TRUE(static_cast<bool>(delegate));
    delegate.invoke(TickEvent{0.0});

    int expectedSum = 0;
    for (int i = 0; i < 128; ++i) expectedSum += i;

    EXPECT_EQ(sum, expectedSum);
}

TEST(FastDelegateTest, MoveSemantics) {
    int result = 0;
    EventHandlerDelegate<TickEvent> d1([&result](const TickEvent& e) {
        result = static_cast<int>(e.deltaTime);
    });

    EventHandlerDelegate<TickEvent> d2 = std::move(d1);

    EXPECT_FALSE(static_cast<bool>(d1));
    EXPECT_TRUE(static_cast<bool>(d2));

    d2.invoke(TickEvent{100.0});
    EXPECT_EQ(result, 100);
}
