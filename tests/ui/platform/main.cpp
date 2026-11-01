// Entry point for the crisp::ui UI3 Win32 platform smoke test suite.
//
// Registers all test cases from test_win32_smoke.cpp (via static-
// initialization registrar), then runs them and reports.

#include <cstdio>

#include "test_harness.h"  // resolved via tests/event include dir

int main() {
    const int failures = ::test::CtTestRegistry::instance().runAll();
    if (failures == 0) {
        std::printf("ALL UI3 PLATFORM TESTS PASSED\n");
        return 0;
    }
    std::printf("%d UI3 PLATFORM TEST(S) FAILED\n", failures);
    return 1;
}
