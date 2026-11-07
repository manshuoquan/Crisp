# ---------------------------------------------------------------------------
# crisp::ui core library (UI1 minimal scaffold)
#
# STATIC library: non-template mechanisms live in .cpp (AGENTS.md §5.3);
# the geometry/trace/state/value types stay in headers. Pure C++17, ZERO Qt
# / Win32 dependency (docs/ui/README.md platform exception clause: the
# platform layer CtPlatformWindow MAY depend on Win32/GDI or Qt — that is
# UI3 and lives separately). Implements docs/ui/04-ui1-architecture.md UI1.
# ---------------------------------------------------------------------------
add_library(crisp_ui STATIC
    ct_geometry.cpp
    ct_widget_trace.cpp
    ct_widget_base.cpp
    ct_paint_types.cpp        # UI2: CtFont operators (others inline)
    ct_painter.cpp            # UI2: state machine + command buffer
    ct_paint_backend_pixel.cpp # UI2: PixelBuffer backend
    ct_backing_store.cpp      # UI2: backing store
    ct_input_mapper.cpp       # UI3: CORE mapper (portable shared
                              #      mapping table ONLY; NO Qt/Win32
                              #      dependency, MUST-7)
    ct_dispatcher_registry.cpp # UI4: per-thread semantic dispatcher registry
    ct_push_button.cpp        # UI4: explicit button state machine
    ct_line_edit.cpp          # UI4: single-line text editor
    ct_label.cpp              # UI4: plain-text label
    ct_box_layout.cpp         # UI4: two-pass box layout
    ct_focus_chain.cpp        # UI4: explicit focus chain
    ct_style_basic.cpp        # UI5: basic style (compile-time palette)
)

# platform layer (WARN-5: REQUIRED on Win32, README exception clause):
if(WIN32)
    target_sources(crisp_ui PRIVATE
        platform/ct_input_mapper_win32.cpp   # Win32 MSG -> CtRawMsg -> mapWin32*
        platform/ct_win32_event_source.cpp
        platform/ct_platform_window_win32.cpp
        platform/ct_paint_backend_gdi.cpp)
endif()

target_include_directories(crisp_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../event>
    $<INSTALL_INTERFACE:include>)

target_compile_features(crisp_ui PUBLIC cxx_std_17)

# crisp::ui consumes the event subsystem (CtReceiver base, payloads).
target_link_libraries(crisp_ui PUBLIC crisp::event)

# Qt-host mode (C1: Qt-enabled branch; bridge payloads are Qt-dependent,
# MUST-7 keeps them out of the core library; UI3 W-1 adds the host source).
# NOTE: this subdirectory is added BEFORE the top-level find_package(Qt6)
# probe, so it must probe Qt itself (idempotent cache hit).
find_package(Qt6 COMPONENTS Core Gui QUIET)
if(Qt6_FOUND)
    target_sources(crisp_ui PRIVATE
        platform/ct_input_mapper_bridge.cpp)      # mapBridge* (Qt-host)
    target_sources(crisp_ui PRIVATE
        platform/ct_qt_host_event_source.cpp)     # CtQtHostEventSource<CtUiEventSet> (W-1)
    target_link_libraries(crisp_ui PRIVATE
        Qt6::Core Qt6::Gui)                       # QEvent/QKeyEvent (QtGui)
endif()

# Alias so consumers can link crisp::ui
add_library(crisp::ui ALIAS crisp_ui)
