#include <gtest/gtest.h>
#include <corium/corium.hpp>

using namespace corium;

struct FilterTelemetryEvent {
    uint32_t sensorId;
    float value;
};

using FilterTestEvents = std::variant<QuitEvent, FilterTelemetryEvent>;

TEST(EventFilteringTest, PredicateFilteredDispatch)
{
    using TestRuntime = RuntimeBuilder
        ::WithEvents<FilterTestEvents>
        ::Build;

    class FilterApp : public Application<FilterApp, FilterTestEvents> {
    public:
        uint32_t sensor1Count = 0;
        uint32_t sensor2Count = 0;
        uint32_t highValueCount = 0;

        void onRegisterHandlers() {
            // Filter 1: Sensor ID == 1
            on(
                [](const FilterTelemetryEvent& e) { return e.sensorId == 1; },
                [this](const FilterTelemetryEvent&) { sensor1Count++; }
            );

            // Filter 2: Sensor ID == 2
            on(
                [](const FilterTelemetryEvent& e) { return e.sensorId == 2; },
                [this](const FilterTelemetryEvent&) { sensor2Count++; }
            );

            // Filter 3: Value > 50.0f
            on(
                [](const FilterTelemetryEvent& e) { return e.value > 50.0f; },
                [this](const FilterTelemetryEvent&) { highValueCount++; }
            );
        }
    };

    TestRuntime runtime;
    FilterApp app;
    runtime.initialize(app);

    auto sink = runtime.eventSink();

    sink.post(FilterTelemetryEvent{1, 25.0f}); // sensor1: match, sensor2: no, high: no
    sink.post(FilterTelemetryEvent{2, 75.0f}); // sensor1: no, sensor2: match, high: match
    sink.post(FilterTelemetryEvent{1, 90.0f}); // sensor1: match, sensor2: no, high: match
    sink.post(FilterTelemetryEvent{3, 10.0f}); // sensor1: no, sensor2: no, high: no

    runtime.drain();

    EXPECT_EQ(app.sensor1Count, 2u);
    EXPECT_EQ(app.sensor2Count, 1u);
    EXPECT_EQ(app.highValueCount, 2u);

    runtime.shutdown();
}
