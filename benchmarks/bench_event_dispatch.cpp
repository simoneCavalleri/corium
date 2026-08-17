#include <benchmark/benchmark.h>
#include <corium/corium.hpp>
#include <functional>

using namespace corium;

struct BenchPayload {
    int a;
    int b;
};

using DispatchEvents = std::variant<QuitEvent, BenchPayload>;

static void BM_EventHandlerDelegate_Dispatch(benchmark::State& state) {
    int accumulator = 0;
    EventHandlerDelegate<BenchPayload, 32> delegate([&accumulator](const BenchPayload& p) {
        accumulator += p.a + p.b;
    });

    BenchPayload payload{3, 7};
    for (auto _ : state) {
        delegate.invoke(payload);
        benchmark::DoNotOptimize(accumulator);
    }
}
BENCHMARK(BM_EventHandlerDelegate_Dispatch);

static void BM_StdFunction_Dispatch(benchmark::State& state) {
    int accumulator = 0;
    std::function<void(const BenchPayload&)> fn = [&accumulator](const BenchPayload& p) {
        accumulator += p.a + p.b;
    };

    BenchPayload payload{3, 7};
    for (auto _ : state) {
        fn(payload);
        benchmark::DoNotOptimize(accumulator);
    }
}
BENCHMARK(BM_StdFunction_Dispatch);

static void BM_Reactor_EventDispatch(benchmark::State& state) {
    ReactorT<DispatchEvents, DefaultStoragePolicy> reactor;
    int counter = 0;
    reactor.registerHandler<BenchPayload>([&counter](const BenchPayload& p) {
        counter += p.a + p.b;
    });
    reactor.seal();

    DispatchEvents event = BenchPayload{5, 10};
    for (auto _ : state) {
        reactor.dispatch(event);
        benchmark::DoNotOptimize(counter);
    }
}
BENCHMARK(BM_Reactor_EventDispatch);
