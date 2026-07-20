#include <gtest/gtest.h>
#include <corium/internal/Reactor.hpp>
#include <corium/Events.hpp>

using namespace corium;

TEST(ReactorTest, ExplicitHandlerRegistrationAndDispatch) {
    Reactor reactor;
    int tickCount = 0;

    reactor.registerHandler<TickEvent>([&tickCount](const TickEvent&) {
        tickCount++;
    });

    reactor.seal();

    DefaultEvents event = TickEvent{0.016};
    reactor.dispatch(event);
    reactor.dispatch(event);

    EXPECT_EQ(tickCount, 2);
}

TEST(ReactorTest, AutoDeduceLambdaHandler) {
    Reactor reactor;
    int updateCount = 0;
    int quitCount = 0;

    reactor.registerHandler([&updateCount](const UpdateEvent&) {
        updateCount++;
    });

    reactor.registerHandler([&quitCount](const QuitEvent&) {
        quitCount++;
    });

    reactor.seal();

    DefaultEvents updateEvent = UpdateEvent{0.016};
    DefaultEvents quitEvent = QuitEvent{};

    reactor.dispatch(updateEvent);
    reactor.dispatch(quitEvent);
    reactor.dispatch(updateEvent);

    EXPECT_EQ(updateCount, 2);
    EXPECT_EQ(quitCount, 1);
}

TEST(ReactorTest, MultipleHandlersForSameEvent) {
    Reactor reactor;
    int h1Count = 0;
    int h2Count = 0;

    reactor.registerHandler([&h1Count](const TickEvent&) { h1Count++; });
    reactor.registerHandler([&h2Count](const TickEvent&) { h2Count++; });

    reactor.seal();

    DefaultEvents event = TickEvent{0.1};
    reactor.dispatch(event);

    EXPECT_EQ(h1Count, 1);
    EXPECT_EQ(h2Count, 1);
}
