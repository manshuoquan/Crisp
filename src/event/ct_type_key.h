#pragma once

// CtTypeKey: address-derived compile-time type identity.
//
// Part of the CRISP event subsystem (crisp::event). Replaces v0's
// FNV-1a(name) hashing with a collision-free, TU-stable key: the address of
// a per-type inline static data member.

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace crisp::event::detail {

/// FNV-1a 64-bit hash, kept ONLY for debug/diagnostic name hashing.
/// NOT used for type identity — see CtTypeKey. Moved here from v0's
/// ct_event.h so no public header depends on a hashing function.
constexpr uint64_t fnv1a(const char* s,
                         uint64_t seed = 14695981039346656037ull) noexcept {
    uint64_t h = seed;
    for (; *s != '\0'; ++s) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(*s));
        h *= 1099511628211ull;
    }
    return h;
}

/// Empty tag type whose address serves as a unique, TU-stable identity.
struct CtTypeAnchor {};

} // namespace crisp::event::detail

namespace crisp::event {

/// Address-derived compile-time type key. Unique per type across TUs.
///
/// The key is the address of a function-template-scoped inline static data
/// member. C++17 guarantees exactly one entity per (template, argument)
/// specialization across the whole program, so `CtTypeKey<A>::value()` and
/// `CtTypeKey<B>::value()` can never collide — no hashing, no RTTI.
template <typename T>
struct CtTypeKey {
    using value_type = uint64_t;

    /// The unique key: address of the anchor object.
    /// (Rev 4, P2-11) The reinterpret_cast is the SOLE sanctioned C-style
    /// cast in the project (§6.1) — pointer-to-integer conversion between
    /// uintptr_t and uint64_t is standard reinterpret_cast usage.
    static value_type value() noexcept {
        return reinterpret_cast<value_type>(&s_anchor);
    }

    /// Anchor object; its address is the identity.
    static inline const detail::CtTypeAnchor s_anchor{};

    CtTypeKey() = delete; ///< stateless; never instantiate
};

} // namespace crisp::event

/// Macro that expands to a specialization of CtEventTraits.
/// Plain preprocessor expansion of a template specialization — NOT code
/// generation (no moc, no qmake, no reflection). Call at file scope,
/// outside any namespace.
#define CRISP_EVENT_TYPE(TYPE, NAME)                                    \
    static_assert(!std::is_same_v<TYPE, void>,                          \
                  "CRISP_EVENT_TYPE: TYPE must be a concrete type");    \
    namespace crisp::event {                                            \
    template <>                                                         \
    struct CtEventTraits<TYPE> {                                        \
        static constexpr bool kIsValid = true;                          \
        static constexpr std::string_view name = NAME;                  \
    };                                                                  \
    }
