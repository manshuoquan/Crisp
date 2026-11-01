// Entry point for the crisp::ui UI3 Qt-host mapper test suite.
//
// Registers all test cases from test_ct_input_mapper_qt.cpp (via static-
// initialization registrar), then runs them and reports.

#include <cstdio>

#include "test_harness.h"

int main() {
    const int failures = ::test::CtTestRegistry::instance().runAll();
    if (failures == 0) {
        std::printf("ALL UI3 QT MAPPER TESTS PASSED\n");
        return 0;
    }
    std::printf("%d UI3 QT MAPPER TEST(S) FAILED\n", failures);
    return 1;
}
