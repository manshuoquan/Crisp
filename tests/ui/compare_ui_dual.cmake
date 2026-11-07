# compare_ui_dual.cmake — widget-tree dual-run comparator (UI3 O4, §17).
#
# Part of the CRISP project. Compares the semantic-event logs written by
# the two UI dual-run backends (ui_win32.log vs ui_qt.log) byte-for-byte.
# Runs as a CMake script: `cmake -P compare_ui_dual.cmake` from the build
# directory of tests/ui (where both backends ran via ctest, with
# WORKING_DIRECTORY set and DEPENDS fixture order).
#
# ctest fixture semantics: dual_run_ui_win32 / dual_run_ui_qt MUST have
# run before this script (set_tests_properties DEPENDS in CMakeLists.txt).

set(win32_log "${CMAKE_CURRENT_BINARY_DIR}/ui_win32.log")
set(qt_log "${CMAKE_CURRENT_BINARY_DIR}/ui_qt.log")

# When run via `cmake -P` (no project context) CMAKE_CURRENT_BINARY_DIR is
# empty; the working directory IS the tests/ui build dir.
if(NOT win32_log OR NOT qt_log)
    set(win32_log "ui_win32.log")
    set(qt_log "ui_qt.log")
endif()

if(NOT EXISTS "${win32_log}")
    message(FATAL_ERROR "ui_win32.log not found at ${win32_log}")
endif()
if(NOT EXISTS "${qt_log}")
    message(FATAL_ERROR "ui_qt.log not found at ${qt_log}")
endif()

# Byte-for-byte comparison. WARN-3 fix: an EMPTY log is a failure (the
# backend removed its stale log then failed to write — an empty-vs-empty
# byte match would be a false green).
file(READ "${win32_log}" win32_contents)
file(READ "${qt_log}" qt_contents)
string(LENGTH "${win32_contents}" win32_len)
string(LENGTH "${qt_contents}" qt_len)
if(win32_len EQUAL 0)
    message(FATAL_ERROR "ui_win32.log is EMPTY (backend failed to write?)")
endif()
if(qt_len EQUAL 0)
    message(FATAL_ERROR "ui_qt.log is EMPTY (backend failed to write?)")
endif()
if(NOT win32_contents STREQUAL qt_contents)
    message(FATAL_ERROR
        "UI dual-run mismatch:\n--- ui_win32.log ---\n${win32_contents}\n--- ui_qt.log ---\n${qt_contents}")
endif()

message(STATUS "ui dual-run: widget-tree semantic logs byte-identical "
               "(${qt_contents} lines)")
