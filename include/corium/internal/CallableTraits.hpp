#pragma once

#include <type_traits>

namespace corium {

template <typename T>
struct callable_event_type;

// Const member function operator() (const lambda)
template <typename C, typename R, typename EventType>
struct callable_event_type<R(C::*)(const EventType&) const> {
    using type = std::decay_t<EventType>;
};

// Non-const member function operator() (mutable lambda)
template <typename C, typename R, typename EventType>
struct callable_event_type<R(C::*)(const EventType&)> {
    using type = std::decay_t<EventType>;
};

// Value parameter const operator()
template <typename C, typename R, typename EventType>
struct callable_event_type<R(C::*)(EventType) const> {
    using type = std::decay_t<EventType>;
};

// Value parameter non-const operator()
template <typename C, typename R, typename EventType>
struct callable_event_type<R(C::*)(EventType)> {
    using type = std::decay_t<EventType>;
};

// Free function pointer (const ref)
template <typename R, typename EventType>
struct callable_event_type<R(*)(const EventType&)> {
    using type = std::decay_t<EventType>;
};

// Free function pointer (value)
template <typename R, typename EventType>
struct callable_event_type<R(*)(EventType)> {
    using type = std::decay_t<EventType>;
};

template <typename Callable, typename = void>
struct get_callable_event_type;

template <typename Callable>
struct get_callable_event_type<Callable, std::void_t<decltype(&std::decay_t<Callable>::operator())>> {
    using type = typename callable_event_type<decltype(&std::decay_t<Callable>::operator())>::type;
};

template <typename R, typename EventType>
struct get_callable_event_type<R(*)(const EventType&)> {
    using type = std::decay_t<EventType>;
};

template <typename R, typename EventType>
struct get_callable_event_type<R(*)(EventType)> {
    using type = std::decay_t<EventType>;
};

/// @brief Helper trait to deduce the concrete EventType argument from a callable (lambda, function pointer, or functor).
template <typename Callable>
using callable_event_type_t = typename get_callable_event_type<Callable>::type;

} // namespace corium
