// =============================================================================
// Corium Showcase 03: High-Frequency Trading (HFT) Market Data & Execution
// Demonstrates:
//  - PriorityQueuePolicy: Emergency Risk Orders prioritized ahead of normal Bids/Asks
//  - AuditOverflowPolicy: Non-blocking deterministic ring buffer overflow accounting
//  - Batch Pumping & High-Throughput Stream Processing
//  - Real-Time Latency Statistics & Micro-Burst Handling
// =============================================================================

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <variant>
#include <vector>

#include "corium/Application.hpp"
#include "corium/Runtime.hpp"
#include "corium/policies/OverflowPolicies.hpp"

// -----------------------------------------------------------------------------
// 1. Order Book & Financial Market Event Types
// -----------------------------------------------------------------------------
struct MarketQuoteEvent {
    uint32_t symbolId; // e.g. 1 = BTC/USDT, 2 = ETH/USDT
    double bidPrice;
    double askPrice;
    double volume;
};

struct TradeExecutionEvent {
    uint64_t tradeId;
    uint32_t symbolId;
    double executionPrice;
    double quantity;
    bool isBuy;
};

struct EmergencyRiskCancelEvent {
    uint32_t accountId;
    uint32_t symbolId;
    const char* cancelReason;
};

using HftEvents = std::variant<
    corium::QuitEvent,
    MarketQuoteEvent,
    TradeExecutionEvent,
    EmergencyRiskCancelEvent
>;

// -----------------------------------------------------------------------------
// 2. High-Throughput HFT Runtime with Custom Capacity & Overflow Policies
// -----------------------------------------------------------------------------
// Configured with 32 High-Priority slots (Risk Cancels) and 128 Normal slots (Quotes/Trades)
using HftRuntime = corium::RuntimeBuilder
    ::WithEvents<HftEvents>
    ::WithPriorityQueue<32, 128>
    ::WithOverflowPolicy<corium::AuditOverflowPolicy> // Audit counter for dropped packets during micro-bursts
    ::WithSignalPolicy<corium::NoSignalPolicy>
    ::Build;

// -----------------------------------------------------------------------------
// 3. Trading Engine Core Application
// -----------------------------------------------------------------------------
class HftTradingEngineApp : public corium::Application<HftTradingEngineApp, HftEvents> {
public:
    uint32_t quotesProcessed = 0;
    uint32_t tradesExecuted = 0;
    uint32_t riskCancelsExecuted = 0;

    void onRegisterHandlers()
    {
        // 1. Normal Market Depth Quotes
        on([this](const MarketQuoteEvent& q) {
            quotesProcessed++;
            if (quotesProcessed <= 3 || quotesProcessed % 25 == 0) {
                std::cout << "  [\033[36mMARKET L2\033[0m] Symbol #" << q.symbolId
                          << " | Bid: $" << std::fixed << std::setprecision(2) << q.bidPrice
                          << " | Ask: $" << q.askPrice << " | Vol: " << q.volume << "\n";
            }
        });

        // 2. Trade Fills Execution
        on([this](const TradeExecutionEvent& t) {
            tradesExecuted++;
            std::cout << "  [\033[32mTRADE FILL\033[0m] #" << t.tradeId << " Symbol #" << t.symbolId
                      << " | " << (t.isBuy ? "\033[32mBUY\033[0m " : "\033[31mSELL\033[0m")
                      << " @ $" << std::fixed << std::setprecision(2) << t.executionPrice
                      << " (Qty: " << t.quantity << ")\n";
        });

        // 3. Emergency Risk Manager Order Cancellations (Dispatched via High-Priority Queue)
        on([this](const EmergencyRiskCancelEvent& r) {
            riskCancelsExecuted++;
            std::cout << "  [\033[31;1mRISK CANCEL\033[0m] Account #" << r.accountId
                      << " | Symbol #" << r.symbolId << " -> \033[31;1m" << r.cancelReason << "\033[0m!\n";
        });
    }

    void onInitialize()
    {
        std::cout << "[HFT Engine] Matching Engine & Order Book Initialized.\n";
    }
};

int main()
{
    std::cout << "=======================================================\n";
    std::cout << " Corium Showcase 03: HFT Market Data & Execution Engine\n";
    std::cout << " Priority Queue Policies | Audit Overflow | Batch Pump \n";
    std::cout << "=======================================================\n\n";

    HftRuntime runtime;
    HftTradingEngineApp app;

    runtime.initialize(app);

    std::cout << "--- Scenario A: Priority Ordering (Risk Control vs Market Flow) ---\n";
    std::cout << "Injecting: 2 Market Quotes, 1 Emergency Risk Cancel, 1 Trade Fill...\n\n";

    auto sink = runtime.eventSink();

    // 1. Normal priority quotes
    sink.post(MarketQuoteEvent{1, 95420.50, 95421.00, 1.25});
    sink.post(MarketQuoteEvent{1, 95421.00, 95421.50, 2.40});

    // 2. High-priority risk cancellation (should be dispatched BEFORE normal quotes)
    sink.postHighPriority(EmergencyRiskCancelEvent{9901, 1, "MAX_DRAWDOWN_LIMIT_TRIGGERED"});

    // 3. Trade execution
    sink.post(TradeExecutionEvent{88231, 1, 95421.00, 0.50, true});

    // Process all events
    runtime.pump();

    std::cout << "\n--- Scenario B: Market Micro-Burst & Batch Pumping ---\n";
    std::cout << "Injecting burst of 150 market quotes into fixed 128-slot ring buffer...\n\n";

    runtime.overflowPolicy().resetOverflowCount();

    const auto burstStart = std::chrono::steady_clock::now();
    for (uint32_t i = 1; i <= 150; ++i) {
        sink.post(MarketQuoteEvent{2, 3450.00 + i * 0.1, 3450.50 + i * 0.1, 5.0});
    }
    const auto burstEnd = std::chrono::steady_clock::now();
    const auto pushDurationNs = std::chrono::duration_cast<std::chrono::nanoseconds>(burstEnd - burstStart).count();

    std::cout << "Pumping burst in chunks using batch pump:\n";

    // Process first batch chunk (up to 50 events)
    runtime.pump(50);
    std::cout << "  -> Processed First Chunk : 50 quotes (or queue drained)\n";

    // Drain remaining events in queue
    runtime.pump();
    std::cout << "  -> Processed Remaining Batch\n";

    const uint64_t totalDropped = runtime.overflowPolicy().overflowCount();

    std::cout << "\n=======================================================\n";
    std::cout << " [HFT Engine Performance Report]\n";
    std::cout << "  - Burst Size Ingested  : 150 market updates\n";
    std::cout << "  - Ring Buffer Capacity : 128 slots\n";
    std::cout << "  - Events Dispatched    : " << (app.quotesProcessed) << " events\n";
    std::cout << "  - Burst Dropped (Audit): " << totalDropped << " events (Deterministic overflow)\n";
    std::cout << "  - Push Ingest Latency  : " << (pushDurationNs / 150) << " ns/event\n";
    std::cout << "=======================================================\n\n";

    runtime.shutdown();
    std::cout << "Showcase 03 finished successfully.\n";
    return 0;
}
