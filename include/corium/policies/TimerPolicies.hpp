#pragma once

#include <cstddef>
#include "corium/timers/TimerScheduler.hpp"

namespace corium {

/// @brief Compile-time policy configuring TimerScheduler capacity.
/// @tparam MaxTimers Maximum number of concurrent timers allowed (default: 64).
template <size_t MaxTimers = 64>
struct FixedTimerStoragePolicy {
    static constexpr size_t max_timers = MaxTimers;
};

using DefaultTimerStoragePolicy = FixedTimerStoragePolicy<64>;
using CompactTimerStoragePolicy = FixedTimerStoragePolicy<16>;
using LargeTimerStoragePolicy = FixedTimerStoragePolicy<256>;

} // namespace corium
