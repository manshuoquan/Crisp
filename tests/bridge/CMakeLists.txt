# ---------------------------------------------------------------------------
# M3/M4 Qt bridge tests (hand-written test doubles, NO mock framework)
#
# Built only when Qt6 was found (guarded by the top-level CMakeLists.txt).
# The bridge test executables need Qt6Core.dll on the PATH at runtime
# (D:\Qt\6.2.4\bin) — see docs/event-system/05-qt-bridge.md §5.2 contract #5.
# ---------------------------------------------------------------------------
find_package(Qt6 COMPONENTS Core Gui Widgets QUIET)

add_executable(qt_bridge_tests
    main.cpp
    test_qt_payload.cpp
    test_qt_translator.cpp
    test_qt_event_source.cpp)

# Qt6::Widgets: the dual-run backends use QWidget as the Qt-side receiver
# (the pure-Qt backend's CtDualWidget drives business via widget event
# handlers; the bridge backend's capture tests use a plain QObject receiver,
# §6.3).
target_link_libraries(qt_bridge_tests PRIVATE
    crisp::qt_bridge crisp::event Qt6::Core Qt6::Widgets)

add_test(NAME qt_bridge_tests COMMAND qt_bridge_tests)

# ---------------------------------------------------------------------------
# Dual-run verification (M3 §6): two executables run the SAME business logic
# (business_logic.h) on two backends, write their logs to files, and ctest
# compares the files byte-for-byte + exit codes (§6.2, Rev 2 P3-2).
# ---------------------------------------------------------------------------

add_executable(dual_main_qt dual/main_qt.cpp)
target_link_libraries(dual_main_qt PRIVATE Qt6::Core Qt6::Widgets)

add_executable(dual_main_bridge dual/main_bridge.cpp)
target_link_libraries(dual_main_bridge PRIVATE
    crisp::qt_bridge crisp::event Qt6::Core Qt6::Widgets)

# Each backend writes its own log file; the comparator test then diffs them.
add_test(NAME dual_run_qt COMMAND dual_main_qt --log=qt.log)
add_test(NAME dual_run_bridge COMMAND dual_main_bridge --log=bridge.log)

# Byte-for-byte comparison + exit-code comparison (Rev 2 P3-2, Windows has
# no fork). Runs AFTER both backends via ctest fixture dependency. The
# comparator script lives in the source tree; run it with an explicit
# absolute path and the build dir as the working directory (where both
# backends wrote their logs).
add_test(NAME dual_compare
    COMMAND ${CMAKE_COMMAND}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/compare_logs.cmake)
set_tests_properties(dual_compare PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS "dual_run_qt;dual_run_bridge")
