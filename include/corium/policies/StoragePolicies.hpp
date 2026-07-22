#pragma once

#include <cstddef>

namespace corium {

/// @brief Policy configuring compile-time handler capacity and delegate inline storage size.
/// @tparam MaxHandlers Maximum handlers per event type stored statically.
/// @tparam InlineSize Maximum bytes for FastDelegate inline storage (zero heap allocation).
template <std::size_t MaxHandlers = 8, std::size_t InlineSize = 32>
struct FixedStoragePolicy {
    static constexpr std::size_t max_handlers_per_event = MaxHandlers;
    static constexpr std::size_t inline_storage_size = InlineSize;
};

/// @brief Default storage policy (8 handlers per event type, 32 bytes inline delegate storage).
using DefaultStoragePolicy = FixedStoragePolicy<8, 32>;

/// @brief Footprint-optimized storage policy (4 handlers per event type, 16 bytes inline storage).
using CompactStoragePolicy = FixedStoragePolicy<4, 16>;

/// @brief High-capacity storage policy (16 handlers per event type, 64 bytes inline storage).
using LargeStoragePolicy = FixedStoragePolicy<16, 64>;

} // namespace corium
