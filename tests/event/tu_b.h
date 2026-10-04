// tu_b.h — cross-TU type-key test declarations (TU B).
//
// Part of the CRISP event subsystem test suite. Declares the key accessors
// implemented in tu_b.cpp; the main test binary compares keys from both
// translation units (§13.2, §6.1 契约 #1/#3).

#pragma once

#include <cstdint>

namespace tu_b {

/// CtTypeKey<CrossTuPayload>::value() as seen from TU B.
std::uint64_t sharedKey() noexcept;

/// CtTypeKey<TuBPayload>::value() — TU-B-unique payload key.
std::uint64_t uniqueKey() noexcept;

} // namespace tu_b
