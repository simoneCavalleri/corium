#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>

#include "corium/async/Task.hpp"

namespace corium::async {

namespace detail {

template <typename T>
struct WrapVoid {
    using type = T;
};

template <>
struct WrapVoid<void> {
    using type = std::monostate;
};

template <typename T>
using wrap_void_t = typename WrapVoid<T>::type;

} // namespace detail

/// @brief Result container for whenAny combinator.
template <typename... ResultTypes>
struct WhenAnyResult {
    std::size_t index{0};
    std::variant<detail::wrap_void_t<ResultTypes>...> result{};
};

namespace detail {

template <size_t Index, typename TaskType, typename ResultVariant>
bool checkTaskDone(TaskType& task, size_t& winnerIndex, ResultVariant& resultVariant)
{
    if (task.done()) {
        winnerIndex = Index;
        if constexpr (!std::is_void_v<typename TaskType::ValueType>) {
            resultVariant.template emplace<Index>(task.await_resume());
        } else {
            task.await_resume();
            resultVariant.template emplace<Index>(std::monostate{});
        }
        return true;
    }
    return false;
}

template <size_t Index, typename TaskType>
void resumeTask(TaskType& task)
{
    if (!task.done()) {
        task.resume();
    }
}

template <typename... Tasks, size_t... Is>
Task<WhenAnyResult<typename std::decay_t<Tasks>::ValueType...>> whenAnyImpl(std::index_sequence<Is...>, Tasks... tasks)
{
    using ResultType = WhenAnyResult<typename std::decay_t<Tasks>::ValueType...>;
    ResultType res{};

    // Initial resume pass
    (resumeTask<Is>(tasks), ...);

    while (true) {
        bool winnerFound = (checkTaskDone<Is>(tasks, res.index, res.result) || ...);
        if (winnerFound) {
            break;
        }
        (resumeTask<Is>(tasks), ...);
    }

    co_return res;
}

} // namespace detail

/// @ingroup async
/// @brief Awaits the first task among multiple tasks to complete.
/// @return Task containing WhenAnyResult with index and variant result.
template <typename... Tasks>
auto whenAny(Tasks&&... tasks)
{
    return detail::whenAnyImpl(
        std::index_sequence_for<Tasks...>{},
        std::forward<Tasks>(tasks)...
    );
}

} // namespace corium::async
