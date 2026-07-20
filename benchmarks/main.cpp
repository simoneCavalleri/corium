// =============================================================================
// Corium Benchmark Suite
//
// Measures:
//   1. Reactor & FastDelegate O(1) Dispatch Latency.
//   2. Lock-Free MPSC RingBuffer Push Throughput across 1, 2, 4, 8 threads.
//   3. Lock-Free MPSC RingBuffer Pop Latency.
//   4. Runtime End-To-End Post & Pump Throughput.
//   5. Signal Policy Performance Comparison.
// =============================================================================

#include <benchmark/benchmark.h>
#include <corium/corium.hpp>

#include <atomic>
#include <thread>
#include <vector>

using namespace corium;

// =============================================================================
// 1. Reactor & FastDelegate O(1) Dispatch Benchmark
// =============================================================================

static void BM_ReactorDispatch(benchmark::State& state) {
    Reactor reactor;
    reactor.registerHandler([](const TickEvent& e) {
        double dt = e.deltaTime;
        benchmark::DoNotOptimize(dt);
    });
    reactor.seal();

    TickEvent event{0.016};

    for (auto _ : state) {
        reactor.dispatch(event);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ReactorDispatch);

// =============================================================================
// 2. Lock-Free MPSC RingBuffer Push Throughput Across Threads
// =============================================================================

static void BM_MpscRingBufferPush(benchmark::State& state) {
    MpscRingBuffer<TickEvent, 65536> ringBuffer;

    for (auto _ : state) {
        auto res = ringBuffer.tryPush(TickEvent{0.001});
        benchmark::DoNotOptimize(res);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MpscRingBufferPush)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

// =============================================================================
// 3. Lock-Free MPSC RingBuffer Pop Latency
// =============================================================================

static void BM_MpscRingBufferPop(benchmark::State& state) {
    static constexpr size_t BatchSize = 1024;
    MpscRingBuffer<TickEvent, 65536> ringBuffer;
    TickEvent event{0.001};

    for (auto _ : state) {
        state.PauseTiming();
        for (size_t i = 0; i < BatchSize; ++i) {
            ringBuffer.tryPush(TickEvent{0.001});
        }
        state.ResumeTiming();

        for (size_t i = 0; i < BatchSize; ++i) {
            bool popped = ringBuffer.tryPop(event);
            benchmark::DoNotOptimize(popped);
        }
    }

    state.SetItemsProcessed(state.iterations() * BatchSize);
}
BENCHMARK(BM_MpscRingBufferPop);

// =============================================================================
// 4. Runtime End-To-End Post & Pump Throughput
// =============================================================================

class BenchApp : public AppCore {
protected:
    void onRegisterHandlers() override {
        on([](const TickEvent& e) {
            double dt = e.deltaTime;
            benchmark::DoNotOptimize(dt);
        });
    }

    void onInitialize() override {}
};

static void BM_RuntimePostAndPump(benchmark::State& state) {
    Runtime runtime;
    BenchApp app;
    runtime.initialize(app);

    TickEvent event{0.016};

    for (auto _ : state) {
        runtime.eventSink().post(event);
        runtime.pump();
    }

    runtime.shutdown();
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RuntimePostAndPump);

// =============================================================================
// 5. Signal Policy Comparison (NoSignalPolicy vs AtomicWaitSignalPolicy vs CallbackSignalPolicy)
// =============================================================================

static void BM_SignalPolicy_NoSignal(benchmark::State& state) {
    using RealtimeRuntime = RuntimeBuilder<>
        ::WithSignalPolicy<NoSignalPolicy>
        ::Build;

    RealtimeRuntime runtime;
    BenchApp app;
    runtime.initialize(app);

    TickEvent event{0.016};

    for (auto _ : state) {
        runtime.eventSink().post(event);
        runtime.pump();
    }

    runtime.shutdown();
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SignalPolicy_NoSignal);

static void BM_SignalPolicy_AtomicWait(benchmark::State& state) {
    using FutexRuntime = RuntimeBuilder<>
        ::WithSignalPolicy<AtomicWaitSignalPolicy>
        ::Build;

    FutexRuntime runtime;
    BenchApp app;
    runtime.initialize(app);

    TickEvent event{0.016};

    for (auto _ : state) {
        runtime.eventSink().post(event);
        runtime.pump();
    }

    runtime.shutdown();
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SignalPolicy_AtomicWait);

BENCHMARK_MAIN();
