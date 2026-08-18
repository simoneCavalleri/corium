#pragma once

#include "corium/ipc/UdsChannel.hpp"

namespace corium::ipc {

/// @ingroup ipc
/// @brief Platform-agnostic inter-process datagram channel.
/// Resolves to UdsChannel on POSIX/UNIX systems and supported Windows platforms.
template <typename EventVariant>
using PlatformChannel = UdsChannel<EventVariant>;

} // namespace corium::ipc
