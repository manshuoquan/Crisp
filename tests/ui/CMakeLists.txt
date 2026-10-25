# ---------------------------------------------------------------------------
# Unit tests for crisp::ui (UI1; hand-written test doubles, no mock)
# ---------------------------------------------------------------------------
add_executable(ui_tests
    main.cpp
    test_ct_geometry.cpp
    test_ct_widget_trace.cpp
    test_ct_widget_base.cpp)

# Reuse the event test harness header (project rule: hand-written, no mock).
target_include_directories(ui_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../event)

target_link_libraries(ui_tests PRIVATE crisp::ui crisp::event)

add_test(NAME ui_tests COMMAND ui_tests)
