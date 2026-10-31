// alloc_tracking.h — shared allocation-tracking declarations for the
// crisp::ui test binary (UI1 zero-alloc tests, UI2 zero-alloc tests).
//
// The counting operator new/delete pair is defined ONCE in
// test_ct_widget_base.cpp (the UI1 allocation-tracking anchor); every
// other test TU that needs to assert zero allocation includes this header
// and uses the shared counters (linkage: extern, defined in the anchor TU).
//
// Hand-written test double; no mock framework (project rule).

#pragma once

#include <atomic>
#include <cstddef>

/// Heap-allocation counter (armed by g_counting; see the operator
/// new/delete definitions in test_ct_widget_base.cpp).
extern std::atomic<std::size_t> g_allocCount;
/// Master switch: when true, operator new increments g_allocCount.
extern std::atomic<bool> g_counting;
