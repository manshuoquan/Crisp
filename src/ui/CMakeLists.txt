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
    ct_widget_base.cpp)

target_include_directories(crisp_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>)

target_compile_features(crisp_ui PUBLIC cxx_std_17)

# crisp::ui consumes the event subsystem (CtReceiver base, payloads).
target_link_libraries(crisp_ui PUBLIC crisp::event)

# Alias so consumers can link crisp::ui
add_library(crisp::ui ALIAS crisp_ui)
