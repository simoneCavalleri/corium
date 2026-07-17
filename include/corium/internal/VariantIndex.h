#pragma once

#include <type_traits>
#include <variant>

namespace corium {

template <typename T, typename... Types>
constexpr std::size_t get_variant_index_impl() {
    constexpr bool matches[] = { std::is_same_v<T, Types>... };
    
    for (std::size_t i = 0; i < sizeof...(Types); ++i) {
        if (matches[i]) return i;
    }
    
    return static_cast<std::size_t>(-1); 
}

template <typename T, typename Variant>
struct variant_index;

template <typename T, typename... Types>
struct variant_index<T, std::variant<Types...>> {
    static constexpr std::size_t value = get_variant_index_impl<T, Types...>();
    
    static_assert(value != static_cast<std::size_t>(-1), 
                  "ERROR: The requested Event type is not part of the std::variant 'Event'!");
};

template <typename T, typename Variant>
inline constexpr std::size_t variant_index_v = variant_index<T, Variant>::value;

} // namespace corium
