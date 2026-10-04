// Entry point for the crisp::event test suite.
//
// Registers all test cases from test_ct_event.cpp / test_ct_dispatcher.cpp
// (via static-initialization registrar), then runs them and reports.

#include "test_harness.h"

int main() {
    const int failures = ::test::CtTestRegistry::instance().runAll();
    if (failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d TEST(S) FAILED\n", failures);
    return 1;
}
