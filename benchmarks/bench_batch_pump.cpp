#include <benchmark/benchmark.h>
#include <corium/corium.hpp>

using namespace corium;

struct StreamData {
    uint32_t sample;
};

using StreamEvents = std::variant<QuitEvent, StreamData>;

static void BM_EventBus_SinglePump(benchmark::State& state) {
    BasicEventBus<StreamEvents> bus;
    uint32_t sink = 0;
    bus.registerHandler<StreamData>([&sink](const StreamData& s) {
        sink += s.sample;
    });
    bus.seal();

    for (auto _ : state) {
        state.PauseTiming();
        for (uint32_t i = 0; i < 64; ++i) {
            bus.post(StreamData{i});
        }
        state.ResumeTiming();

        while (bus.processOne()) {
            benchmark::DoNotOptimize(sink);
        }
    }
}
BENCHMARK(BM_EventBus_SinglePump);

static void BM_EventBus_BatchPump(benchmark::State& state) {
    BasicEventBus<StreamEvents> bus;
    uint32_t sink = 0;
    bus.registerHandler<StreamData>([&sink](const StreamData& s) {
        sink += s.sample;
    });
    bus.seal();

    for (auto _ : state) {
        state.PauseTiming();
        for (uint32_t i = 0; i < 64; ++i) {
            bus.post(StreamData{i});
        }
        state.ResumeTiming();

        bus.processBatch(64);
        benchmark::DoNotOptimize(sink);
    }
}
BENCHMARK(BM_EventBus_BatchPump);
