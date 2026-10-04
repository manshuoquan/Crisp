#pragma once

// CtEventSet: closed set of event payload types known at compile time.
//
// Part of the CRISP event subsystem (crisp::event). Provides the
// std::variant-based heterogeneous storage (any_type) used by the async
// path and the compile-time membership checks used by CtDispatcher.

#include <type_traits>
#include <variant>

#include "ct_event.h"
#include "ct_type_key.h"

namespace crisp::event {

namespace detail {

/// Compile-time helper: true if all types in Us... are pairwise distinct.
template <typename... Us>
struct AllDistinct;

template <>
struct AllDistinct<> : std::true_type {};

template <typename U, typename... Rest>
struct AllDistinct<U, Rest...>
    : std::integral_constant<bool,
                             (!std::is_same_v<U, Rest> && ...) &&
                                 AllDistinct<Rest...>::value> {};

} // namespace detail

/// A closed set of event payload types known at compile time.
template <typename... Ts>
struct CtEventSet {
    static_assert(sizeof...(Ts) >= 1,
                  "CtEventSet needs at least one payload type");
    static_assert((... && CtEventTraits<Ts>::kIsValid),
                  "every payload type in CtEventSet must specialize CtEventTraits");
    static_assert(detail::AllDistinct<Ts...>::value,
                  "payload types in CtEventSet must be pairwise distinct");

    /// The variant that can hold any event of this set (value semantics).
    using any_type = std::variant<CtEvent<Ts>...>;

    /// Number of payload types in the set.
    static constexpr std::size_t size = sizeof...(Ts);

    /// True if payload type T is a member of this set.
    template <typename T>
    static constexpr bool contains = (std::is_same_v<T, Ts> || ...);
};

/// Convenience alias: the variant capable of holding any event in the set.
template <typename... Ts>
using CtAnyEvent = typename CtEventSet<Ts...>::any_type;

/// Type-safe accessor: returns pointer to CtEvent<T> inside the variant.
/// Returns nullptr if `any` currently holds a different event type.
template <typename T, typename... Ts>
CtEvent<T>* getIf(CtAnyEvent<Ts...>& any) noexcept {
    return std::get_if<CtEvent<T>>(&any);
}

/// Exhaustive visitor helper: calls `fn` for whichever event the variant
/// currently holds. Same semantics as std::visit — this exists only as a
/// convenience with a crisper name for dispatcher internals.
template <typename Visitor, typename... Ts>
decltype(auto) visitEvent(Visitor&& fn, CtAnyEvent<Ts...>& any) {
    return std::visit(std::forward<Visitor>(fn), any);
}

/// Exhaustive visitor helper for the const variant (see above).
template <typename Visitor, typename... Ts>
decltype(auto) visitEvent(Visitor&& fn, const CtAnyEvent<Ts...>& any) {
    return std::visit(std::forward<Visitor>(fn), any);
}

} // namespace crisp::event
