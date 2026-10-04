// tu_b.cpp — cross-TU type-key test, translation unit B (§13.2).
//
// Part of the CRISP event subsystem test suite. Mirrors tu_a.cpp: it
// specializes the SAME CrossTuPayload traits (same name) and exposes its
// key. The main test asserts sharedKey() equality between the two TUs —
// proving the address-derived key is stable across translation units —
// and that each TU's unique key differs from every other key.

#include "event/ct_type_key.h"
#include "event/ct_event.h"

#include <cstdint>
#include <string_view>

// Shared payload type: also specialized in tu_a.cpp with the SAME name.
struct CrossTuPayload {
    int value = 0;
};

namespace crisp::event {
template <>
struct CtEventTraits<CrossTuPayload> {
    static constexpr bool kIsValid = true;
    static constexpr std::string_view name = "CrossTuPayload";
};
} // namespace crisp::event

// TU-unique payload type: only this TU knows it.
struct TuBPayload {
    int value = 0;
};

namespace crisp::event {
template <>
struct CtEventTraits<TuBPayload> {
    static constexpr bool kIsValid = true;
    static constexpr std::string_view name = "TuBPayload";
};
} // namespace crisp::event

namespace tu_b {

std::uint64_t sharedKey() noexcept {
    return crisp::event::CtTypeKey<CrossTuPayload>::value();
}

std::uint64_t uniqueKey() noexcept {
    return crisp::event::CtTypeKey<TuBPayload>::value();
}

} // namespace tu_b
