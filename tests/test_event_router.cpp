#include <gtest/gtest.h>
#include <cstdint>
#include <variant>
#include <vector>

#include "corium/corium.hpp"

namespace {

struct TopicEventA {
    int val;
};

struct TopicEventB {
    double temp;
};

using RouterTestEvents = std::variant<
    corium::QuitEvent,
    TopicEventA,
    TopicEventB
>;

} // namespace

TEST(EventRouterTest, TopicSubscriptionAndFanOut) {
    corium::EventRouter<RouterTestEvents, 4, 4> router;

    std::vector<int> sub1Received;
    std::vector<int> sub2Received;
    std::vector<double> sub3Received;

    // Topic 100 has 2 subscribers for EventA
    EXPECT_TRUE(router.subscribeEvent<TopicEventA>(100, [&](const TopicEventA& e) {
        sub1Received.push_back(e.val);
    }));

    EXPECT_TRUE(router.subscribeEvent<TopicEventA>(100, [&](const TopicEventA& e) {
        sub2Received.push_back(e.val * 2);
    }));

    // Topic 200 has 1 subscriber for EventB
    EXPECT_TRUE(router.subscribeEvent<TopicEventB>(200, [&](const TopicEventB& e) {
        sub3Received.push_back(e.temp);
    }));

    EXPECT_EQ(router.topicCount(), 2u);

    // Publish to Topic 100
    size_t invoked = router.publishEvent(100, TopicEventA{.val = 21});
    EXPECT_EQ(invoked, 2u);

    ASSERT_EQ(sub1Received.size(), 1u);
    EXPECT_EQ(sub1Received[0], 21);
    ASSERT_EQ(sub2Received.size(), 1u);
    EXPECT_EQ(sub2Received[0], 42);

    // Publish to Topic 200
    invoked = router.publishEvent(200, TopicEventB{.temp = 98.6});
    EXPECT_EQ(invoked, 1u);
    ASSERT_EQ(sub3Received.size(), 1u);
    EXPECT_DOUBLE_EQ(sub3Received[0], 98.6);

    // Publish to non-existent topic 300
    invoked = router.publishEvent(300, TopicEventA{.val = 999});
    EXPECT_EQ(invoked, 0u);

    // Clear
    router.clear();
    EXPECT_EQ(router.topicCount(), 0u);
}
