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

/// @brief Helper trait to compute index of type T inside std::variant<Types...> at compile time.
template <typename T, typename Variant>
struct variant_index;

template <typename T, typename... Types>
struct variant_index<T, std::variant<Types...>> {
    static constexpr std::size_t value = get_variant_index_impl<T, Types...>();
    
    static_assert(value != static_cast<std::size_t>(-1), 
                  "ERROR: The requested Event type is not part of the std::variant!");
};

/// @brief Compile-time constant of type T's index in Variant.
template <typename T, typename Variant>
inline constexpr std::size_t variant_index_v = variant_index<T, Variant>::value;

/// @brief Helper trait to check if type T exists inside std::variant<Types...> at compile time.
template <typename T, typename Variant>
struct has_variant_type;

template <typename T, typename... Types>
struct has_variant_type<T, std::variant<Types...>> {
    static constexpr bool value = (std::is_same_v<T, Types> || ...);
};

/// @brief Compile-time boolean indicating if type T exists in Variant.
template <typename T, typename Variant>
inline constexpr bool has_variant_type_v = has_variant_type<T, Variant>::value;

} // namespace corium
