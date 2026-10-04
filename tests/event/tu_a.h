// tu_a.h — cross-TU type-key test declarations (TU A).
//
// Part of the CRISP event subsystem test suite. Declares the key accessors
// implemented in tu_a.cpp; the main test binary compares keys from both
// translation units (§13.2, §6.1 契约 #1/#3).

#pragma once

#include <cstdint>

namespace tu_a {

/// CtTypeKey<CrossTuPayload>::value() as seen from TU A.
std::uint64_t sharedKey() noexcept;

/// CtTypeKey<TuAPayload>::value() — TU-A-unique payload key.
std::uint64_t uniqueKey() noexcept;

} // namespace tu_a
