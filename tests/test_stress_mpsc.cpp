#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "corium/Application.hpp"
#include "corium/Runtime.hpp"
#include "corium/RuntimeBuilder.hpp"
#include "corium/policies/QueuePolicies.hpp"

namespace {

struct StressEvent {
    uint32_t producerId{0};
    uint32_t sequence{0};
};

struct StressPriorityEvent {
    uint32_t producerId{0};
    uint32_t sequence{0};
    uint32_t priorityLevel{0};
};

using StressEventVariant = std::variant<
    corium::QuitEvent,
    StressEvent,
    StressPriorityEvent
>;

} // namespace

TEST(StressTest, MultiProducerLockFreeRingBufferUnderContention) {
    constexpr uint32_t NumProducers = 8;
    constexpr uint32_t EventsPerProducer = 50'000;
    constexpr uint32_t TotalExpectedEvents = NumProducers * EventsPerProducer;

    corium::BoundedMpscQueuePolicy<StressEventVariant, 8192> queue;
    std::atomic<bool> startFlag{false};
    std::atomic<bool> producersDone{false};
    std::atomic<uint32_t> totalReceived{0};

    // Producer threads
    std::vector<std::jthread> producers;
    producers.reserve(NumProducers);

    for (uint32_t p = 0; p < NumProducers; ++p) {
        producers.emplace_back([&queue, &startFlag, p]() {
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (uint32_t seq = 0; seq < EventsPerProducer; ++seq) {
                StressEvent evt{.producerId = p, .sequence = seq};
                while (!queue.tryPush(StressEventVariant{evt}).pushed) {
                    std::this_thread::yield();
                }
            }
        });
    }

    // Consumer thread
    std::jthread consumer([&queue, &producersDone, &totalReceived]() {
        uint32_t received = 0;
        while (!producersDone.load(std::memory_order_acquire) || received < TotalExpectedEvents) {
            StressEventVariant evt;
            if (queue.tryPop(evt)) {
                if (std::holds_alternative<StressEvent>(evt)) {
                    ++received;
                }
            } else {
                std::this_thread::yield();
            }
        }
        totalReceived.store(received, std::memory_order_release);
    });

    // Start benchmark/stress
    startFlag.store(true, std::memory_order_release);

    for (auto& t : producers) {
        if (t.joinable()) {
            t.join();
        }
    }
    producersDone.store(true, std::memory_order_release);

    if (consumer.joinable()) {
        consumer.join();
    }

    EXPECT_EQ(totalReceived.load(), TotalExpectedEvents);
}

TEST(StressTest, MultiProducerRuntimeEventBusContention) {
    constexpr uint32_t NumProducers = 6;
    constexpr uint32_t EventsPerProducer = 20'000;
    constexpr uint32_t TotalExpectedEvents = NumProducers * EventsPerProducer;

    using StressRuntime = corium::RuntimeBuilder
        ::WithEvents<StressEventVariant>
        ::WithCapacity<16384>
        ::WithOverflowPolicy<corium::AuditOverflowPolicy>
        ::Build;

    struct StressApp : corium::Application<StressApp, StressEventVariant> {
        uint32_t processedCount{0};

        void onRegisterHandlers() {
            on([this](const StressEvent&) {
                ++processedCount;
            });
        }
    };

    StressRuntime runtime;
    StressApp app;
    runtime.initialize(app);

    std::atomic<bool> startFlag{false};
    std::atomic<uint32_t> activeProducers{NumProducers};
    auto sink = runtime.eventSink();

    std::vector<std::jthread> producers;
    producers.reserve(NumProducers);

    for (uint32_t p = 0; p < NumProducers; ++p) {
        producers.emplace_back([sink, &startFlag, &activeProducers, p]() mutable {
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (uint32_t seq = 0; seq < EventsPerProducer; ++seq) {
                StressEvent evt{.producerId = p, .sequence = seq};
                sink.post(evt);
            }

            activeProducers.fetch_sub(1, std::memory_order_release);
        });
    }

    startFlag.store(true, std::memory_order_release);

    // Consumer drains concurrently while producers are active
    while (activeProducers.load(std::memory_order_acquire) > 0) {
        runtime.pump();
        std::this_thread::yield();
    }

    for (auto& t : producers) {
        if (t.joinable()) {
            t.join();
        }
    }

    // Drain all remaining queued events
    runtime.drain();

    const uint32_t processed = app.processedCount;
    const uint64_t dropped = runtime.overflowPolicy().overflowCount();
    EXPECT_EQ(static_cast<uint64_t>(processed) + dropped, static_cast<uint64_t>(TotalExpectedEvents));
    EXPECT_GT(processed, 0u);
}
