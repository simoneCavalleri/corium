#include <benchmark/benchmark.h>
#include <corium/corium.hpp>

using namespace corium;

struct BenchEvent {
    uint64_t payload;
};

using BenchVariant = std::variant<QuitEvent, BenchEvent>;

static void BM_RingBuffer_SingleProducer(benchmark::State& state) {
    BoundedMpscQueuePolicy<BenchVariant, 4096> queue;
    uint64_t i = 0;

    for (auto _ : state) {
        queue.tryPush(BenchEvent{++i});
        BenchVariant popped;
        benchmark::DoNotOptimize(queue.tryPop(popped));
    }
}
BENCHMARK(BM_RingBuffer_SingleProducer);

static void BM_PriorityQueue_HighPriorityPush(benchmark::State& state) {
    PriorityMpscQueuePolicy<BenchVariant, 2048, 2048> queue;
    uint64_t i = 0;

    for (auto _ : state) {
        queue.tryPush(BenchEvent{++i}, EventPriority::High);
        BenchVariant popped;
        benchmark::DoNotOptimize(queue.tryPop(popped));
    }
}
BENCHMARK(BM_PriorityQueue_HighPriorityPush);
