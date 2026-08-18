#include <gtest/gtest.h>
#include <array>
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

TEST(FastDelegateTest, CustomInlineSizeExecution) {
    // Large capture fitted into customized 256-byte inline storage
    std::array<char, 128> largeBuffer{};
    for (std::size_t i = 0; i < largeBuffer.size(); ++i) {
        largeBuffer[i] = static_cast<char>(i);
    }

    int sum = 0;
    EventHandlerDelegate<TickEvent, 256> delegate([largeBuffer, &sum](const TickEvent&) {
        for (char b : largeBuffer) {
            sum += b;
        }
    });

    EXPECT_TRUE(static_cast<bool>(delegate));
    delegate.invoke(TickEvent{0.0});

    int expectedSum = 0;
    for (char b : largeBuffer) {
        expectedSum += b;
    }

    EXPECT_EQ(sum, expectedSum);
}

TEST(FastDelegateTest, MoveSemantics) {
    int result = 0;
    EventHandlerDelegate<TickEvent> d1([&result](const TickEvent& e) {
        result = static_cast<int>(e.deltaTime);
    });

    EventHandlerDelegate<TickEvent> d2 = std::move(d1);

    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_FALSE(static_cast<bool>(d1));
    EXPECT_TRUE(static_cast<bool>(d2));

    d2.invoke(TickEvent{100.0});
    EXPECT_EQ(result, 100);
}

TEST(FastDelegateTest, ExactInlineSizeBoundaries) {
    // Exact 32 bytes capture
    struct Exactly32Bytes {
        uint64_t a{1};
        uint64_t b{2};
        uint64_t c{3};
        uint64_t d{4};

        void operator()(const TickEvent&) const noexcept {}
    };
    static_assert(sizeof(Exactly32Bytes) == 32);

    EventHandlerDelegate<TickEvent, 32> d32(Exactly32Bytes{});
    EXPECT_TRUE(static_cast<bool>(d32));
    d32.invoke(TickEvent{0.0});

    // Exact 16 bytes capture
    struct Exactly16Bytes {
        uint64_t a{10};
        uint64_t b{20};

        void operator()(const TickEvent&) const noexcept {}
    };
    static_assert(sizeof(Exactly16Bytes) == 16);

    EventHandlerDelegate<TickEvent, 16> d16(Exactly16Bytes{});
    EXPECT_TRUE(static_cast<bool>(d16));
    d16.invoke(TickEvent{0.0});
}

