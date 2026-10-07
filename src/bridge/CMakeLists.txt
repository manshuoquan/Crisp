# ---------------------------------------------------------------------------
# crisp::qt_bridge library (M3, §5.2)
#
# Qt-dependent bridge library, built ONLY when Qt6 was found (the top-level
# CMakeLists.txt probes Qt6 and guards this subdirectory). The core
# crisp::event library remains Qt-free (§5.2 contract #1).
#
# AUTOMOC is OFF for this target: the capture filter (detail::
# CtQtEventCapture<EventSet> in qt_event_source.h) has NO Q_OBJECT — Qt
# 6.2.4's moc rejects Q_OBJECT inside a class template, and the filter needs
# no meta-object (eventFilter() is a plain virtual). The core library stays
# moc-free (AGENTS.md principle 6).
# ---------------------------------------------------------------------------
add_library(crisp_qt_bridge STATIC
    qt_event_payload.cpp
    qt_translator.cpp
    qt_event_source.cpp
    qt_sink.cpp)

target_include_directories(crisp_qt_bridge PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../event>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

target_link_libraries(crisp_qt_bridge PUBLIC crisp::event Qt6::Core)

target_compile_features(crisp_qt_bridge PUBLIC cxx_std_17)

add_library(crisp::qt_bridge ALIAS crisp_qt_bridge)
