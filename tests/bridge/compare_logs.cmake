# compare_logs.cmake — dual-run verification comparator (M3, §6.2).
#
# Part of the CRISP project. Compares the log files written by the two
# dual-run backends (qt.log vs bridge.log) byte-for-byte and verifies the
# exit codes recorded by ctest (the DEPENDS fixture runs the backends first).
# Runs as a CMake script: `cmake -P compare_logs.cmake` from the build
# directory of tests/bridge.
#
# ctest fixture semantics: dual_run_qt / dual_run_bridge MUST have run before
# this script (set_tests_properties DEPENDS in CMakeLists.txt).

# Locate the log files relative to the current working directory (the
# tests/bridge build directory when driven by ctest — both backends ran
# there with --log=qt.log / --log=bridge.log).
set(qt_log "${CMAKE_CURRENT_BINARY_DIR}/qt.log")
set(bridge_log "${CMAKE_CURRENT_BINARY_DIR}/bridge.log")

# When run via `cmake -P` (no project context) CMAKE_CURRENT_BINARY_DIR is
# empty; the working directory IS the tests/bridge build dir (set via
# WORKING_DIRECTORY in tests/bridge/CMakeLists.txt).
if(NOT qt_log OR NOT bridge_log)
    set(qt_log "qt.log")
    set(bridge_log "bridge.log")
endif()

if(NOT EXISTS "${qt_log}")
    message(FATAL_ERROR "qt.log not found at ${qt_log}")
endif()
if(NOT EXISTS "${bridge_log}")
    message(FATAL_ERROR "bridge.log not found at ${bridge_log}")
endif()

# Byte-for-byte comparison.
file(READ "${qt_log}" qt_contents)
file(READ "${bridge_log}" bridge_contents)
if(NOT qt_contents STREQUAL bridge_contents)
    message(FATAL_ERROR
        "Dual-run mismatch:\n--- qt.log ---\n${qt_contents}\n--- bridge.log ---\n${bridge_contents}")
endif()

message(STATUS "dual-run: log files byte-identical (${qt_contents} lines)")
