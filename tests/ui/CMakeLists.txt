# ---------------------------------------------------------------------------
# Unit tests for crisp::ui (UI1 + UI2; hand-written test doubles, no mock)
# ---------------------------------------------------------------------------
add_executable(ui_tests
    main.cpp
    test_ct_geometry.cpp
    test_ct_widget_trace.cpp
    test_ct_widget_base.cpp
    test_ct_paint_types.cpp
    test_ct_painter.cpp
    test_ct_paint_backend_pixel.cpp
    test_ct_backing_store.cpp
    test_ct_input_mapper.cpp)     # UI3: mapper table-driven tests

# Reuse the event test harness header (project rule: hand-written, no mock).
target_include_directories(ui_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../event)

target_link_libraries(ui_tests PRIVATE crisp::ui crisp::event)

# MinGW: crisp_ui's Win32 platform layer (GDI/user32) pulls the GUI
# subsystem; force the console subsystem so the test main() links.
if(MINGW)
    target_link_options(ui_tests PRIVATE -mconsole)
endif()

add_test(NAME ui_tests COMMAND ui_tests)

# UI3 platform smoke (platform-required, enabled under if(WIN32), P9/W-3):
if(WIN32)
    add_executable(ui3_platform_tests
        platform/main.cpp
        platform/test_win32_smoke.cpp)
    target_include_directories(ui3_platform_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../event)
    target_link_libraries(ui3_platform_tests PRIVATE crisp::ui crisp::event)
    # MinGW: the GDI/user32 imports in crisp_ui (platform layer) pull the
    # GUI subsystem; force the console subsystem so the test main() links
    # (same flag as ui_tests, which links successfully).
    if(MINGW)
        target_link_options(ui3_platform_tests PRIVATE -mconsole)
    endif()
    add_test(NAME ui3_platform_tests COMMAND ui3_platform_tests)
endif()
