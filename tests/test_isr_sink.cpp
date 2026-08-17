#include <gtest/gtest.h>
#include <corium/corium.hpp>

using namespace corium;
using namespace corium::embedded;

struct IsrGpioEvent {
    uint8_t pin;
    uint32_t val;
};

struct EmergencyIsrEvent {
    uint32_t code = 0;
    uint32_t severity = 0;

    constexpr EmergencyIsrEvent() = default;
    constexpr EmergencyIsrEvent(uint32_t c, uint32_t s)
        : code(c), severity(s) {}
};

using IsrTestEvents = std::variant<QuitEvent, IsrGpioEvent, EmergencyIsrEvent>;

TEST(IsrSinkTest, IsrEventSinkNormalAndHighPriority)
{
    using IsrRuntime = RuntimeBuilder<>
        ::WithEvents<IsrTestEvents>
        ::WithPriorityQueue<128, 512>
        ::Build;

    class App : public Application<App, IsrRuntime::EventBusType> {
    public:
        std::vector<std::string> log;

        void onRegisterHandlers() {
            this->on([this](const IsrGpioEvent& e) {
                log.push_back("GPIO:" + std::to_string(e.pin) + ":" + std::to_string(e.val));
            });
            this->on([this](const EmergencyIsrEvent& e) {
                log.push_back("EMERGENCY:" + std::to_string(e.code) + ":" + std::to_string(e.severity));
            });
        }
    };

    IsrRuntime runtime;
    App app;
    runtime.initialize(app);

    auto isrSink = makeIsrSink(runtime.eventSink());

    // Post normal event
    isrSink.postFromIsr(IsrGpioEvent{4, 1});
    // Post high-priority event
    EmergencyIsrEvent emergencyEvt{99, 1};
    isrSink.postHighPriorityFromIsr(emergencyEvt);
    // Post another normal event
    isrSink.postFromIsr(IsrGpioEvent{2, 0});

    runtime.pump();

    ASSERT_EQ(app.log.size(), 3u);
    // High-priority emergency event must be dispatched FIRST
    EXPECT_EQ(app.log[0], "EMERGENCY:99:1");
    EXPECT_EQ(app.log[1], "GPIO:4:1");
    EXPECT_EQ(app.log[2], "GPIO:2:0");

    runtime.shutdown();
}

TEST(IsrSinkTest, FreeRtosIsrSinkTaskWokenTracking)
{
    using IsrRuntime = RuntimeBuilder<>::WithEvents<IsrTestEvents>::Build;
    IsrRuntime runtime;

    class App : public Application<App, IsrRuntime::EventBusType> {
    public:
        int eventsReceived = 0;
        void onRegisterHandlers() {
            this->on([this](const IsrGpioEvent&) {
                eventsReceived++;
            });
        }
    } app;

    runtime.initialize(app);

    auto rtosSink = makeFreeRtosIsrSink(runtime.eventSink());

    BaseType_t higherPriorityTaskWoken = pdFALSE;

    rtosSink.postFromIsr(IsrGpioEvent{12, 1}, &higherPriorityTaskWoken);
    EXPECT_EQ(higherPriorityTaskWoken, pdTRUE);

    // Yield check helper shouldn't crash or error
    FreeRtosIsrSink<decltype(runtime.eventSink())>::yieldFromIsr(higherPriorityTaskWoken);

    runtime.pump();
    EXPECT_EQ(app.eventsReceived, 1);

    runtime.shutdown();
}

TEST(IsrSinkTest, InterruptLockSection)
{
    InterruptLock lock;
    EXPECT_TRUE(lock.isLocked());
    lock.unlock();
    EXPECT_FALSE(lock.isLocked());
    lock.lock();
    EXPECT_TRUE(lock.isLocked());
}
