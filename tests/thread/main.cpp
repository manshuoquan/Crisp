// Entry point for the crisp::thread test suite (MT1).
//
// Registers all test cases from test_ct_thread_pool.cpp etc. (via
// static-initialization registrar), then runs them and reports.

#include "test_harness.h"

int main() {
    const int failures = ::test::CtTestRegistry::instance().runAll();
    if (failures == 0) {
        std::printf("ALL THREAD TESTS PASSED\n");
        return 0;
    }
    std::printf("%d THREAD TEST(S) FAILED\n", failures);
    return 1;
}
