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
    test_ct_input_mapper.cpp     # UI3: mapper table-driven tests
    test_ct_push_button.cpp      # UI4: button state machine + click
    test_ct_line_edit.cpp        # UI4: text editing + semantic events
    test_ct_label.cpp            # UI4: label text + size hint
    test_ct_box_layout.cpp       # UI4: two-pass layout golden cases
    test_ct_focus_chain.cpp      # UI4: focus stepping + trace pairing
    test_ct_style_basic.cpp      # UI5: style palette + paint delegation
)

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

# UI3 Qt-host mapper tests (C1: mapBridge* is Qt-dependent - run under a
# Qt-enabled test target; bridge payloads include QString/Qt::Key):
if(Qt6_FOUND)
    add_executable(ui3_qt_mapper_tests
        platform/main_qt.cpp
        platform/test_ct_input_mapper_qt.cpp)
    target_include_directories(ui3_qt_mapper_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../event)
    target_link_libraries(ui3_qt_mapper_tests PRIVATE
        crisp::ui crisp::event Qt6::Core Qt6::Gui)
    if(MINGW)
        target_link_options(ui3_qt_mapper_tests PRIVATE -mconsole)
    endif()
    add_test(NAME ui3_qt_mapper_tests COMMAND ui3_qt_mapper_tests)
endif()
