#pragma once

#include <tuple>
#include <type_traits>
#include <utility>

#include "corium/async/Task.hpp"

namespace corium::async {

namespace detail {

template <typename... Tasks>
inline constexpr bool all_void_tasks_v = (std::is_void_v<typename std::decay_t<Tasks>::ValueType> && ...);

template <typename... Tasks>
Task<std::tuple<typename std::decay_t<Tasks>::ValueType...>> whenAllValueImpl(Tasks... tasks)
{
    co_return std::tuple<typename std::decay_t<Tasks>::ValueType...>{(co_await tasks)...};
}

template <typename... Tasks>
Task<void> whenAllVoidImpl(Tasks... tasks)
{
    ((void)(co_await tasks), ...);
    co_return;
}

} // namespace detail

/// @ingroup async
/// @brief Awaits concurrent or sequential completion of multiple Task coroutines.
/// @return Task containing std::tuple of results, or Task<void> if all input tasks are void.
template <typename... Tasks>
auto whenAll(Tasks&&... tasks)
{
    if constexpr (detail::all_void_tasks_v<Tasks...>) {
        return detail::whenAllVoidImpl(std::forward<Tasks>(tasks)...);
    } else {
        return detail::whenAllValueImpl(std::forward<Tasks>(tasks)...);
    }
}

} // namespace corium::async
