/**
 * @file TimerPolicies.hpp
 * @ingroup policies
 * @brief Timer scheduler static capacity and storage policies.
 */

#pragma once

#include <cstddef>
#include "corium/timers/ClockPolicies.hpp"
#include "corium/timers/TimerScheduler.hpp"

namespace corium {

/// @brief Compile-time policy configuring TimerScheduler capacity and clock source.
/// @tparam MaxTimers Maximum number of concurrent timers allowed (default: 64).
/// @tparam ClockPolicy Time source and arithmetic policy (default: ChronoClockPolicy).
template <size_t MaxTimers = 64, typename ClockPolicy = ChronoClockPolicy>
struct FixedTimerStoragePolicy {
    static constexpr size_t max_timers = MaxTimers;
    using clock_policy = ClockPolicy;
};

using DefaultTimerStoragePolicy = FixedTimerStoragePolicy<64, ChronoClockPolicy>;
using CompactTimerStoragePolicy = FixedTimerStoragePolicy<16, ChronoClockPolicy>;
using LargeTimerStoragePolicy = FixedTimerStoragePolicy<256, ChronoClockPolicy>;

} // namespace corium
