// tu_a.cpp — cross-TU type-key test, translation unit A (§13.2).
//
// Part of the CRISP event subsystem test suite. Verifies §6.1 契约 #1/#3:
// CtTypeKey<T>::value() is unique per type and STABLE across translation
// units (ODR/ICF concern). Each TU specializes the traits of a shared
// payload type and exposes its key for comparison in the main test binary.
//
// The anchor address (function-template-scoped inline static data member)
// is guaranteed by the C++17 standard to be a single entity per
// (template, argument) specialization across the whole program.

#include "event/ct_type_key.h"
#include "event/ct_event.h"

#include <cstdint>
#include <string_view>

// Shared payload type: also specialized in tu_b.cpp with the SAME name.
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
struct TuAPayload {
    int value = 0;
};

namespace crisp::event {
template <>
struct CtEventTraits<TuAPayload> {
    static constexpr bool kIsValid = true;
    static constexpr std::string_view name = "TuAPayload";
};
} // namespace crisp::event

namespace tu_a {

std::uint64_t sharedKey() noexcept {
    return crisp::event::CtTypeKey<CrossTuPayload>::value();
}

std::uint64_t uniqueKey() noexcept {
    return crisp::event::CtTypeKey<TuAPayload>::value();
}

} // namespace tu_a
