// Entry point for the crisp::ui test suite (UI1).
//
// Registers all test cases from test_ct_geometry.cpp /
// test_ct_widget_trace.cpp / test_ct_widget_base.cpp (via static-
// initialization registrar), then runs them and reports.

#include "test_harness.h"

int main() {
    const int failures = ::test::CtTestRegistry::instance().runAll();
    if (failures == 0) {
        std::printf("ALL UI TESTS PASSED\n");
        return 0;
    }
    std::printf("%d UI TEST(S) FAILED\n", failures);
    return 1;
}
