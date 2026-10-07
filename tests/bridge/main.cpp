// Entry point for the bridge test suite (M3, §5.1).
//
// Registers all test cases from test_qt_payload.cpp / test_qt_translator.cpp
// / test_qt_event_source.cpp (via static-initialization registrar), then runs
// them and reports. Every bridge test runs with a live QApplication instance
// (§7 preamble): created here before the harness runs.

#include <QApplication>

#include "test_harness.h"

int main(int argc, char** argv) {
    // QApplication (NOT QCoreApplication): Qt 6 fail-fasts (0xC0000602) when
    // a QWidget is created under a plain QCoreApplication, and the bridge
    // tests/backends construct widgets (dual-run) or need the GUI event
    // plumbing. The capture tests themselves use a plain QObject receiver
    // (no window-lifecycle events), but the app type must match the Qt
    // module set built for the bridge tests.
    QApplication app(argc, argv);

    const int failures = ::test::CtTestRegistry::instance().runAll();
    if (failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d TEST(S) FAILED\n", failures);
    return 1;
}
