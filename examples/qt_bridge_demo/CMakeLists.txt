# ---------------------------------------------------------------------------
# Qt bridge demo: shows CtQtEventSource (Qt -> CRISP) + postToQt (CRISP -> Qt)
# Built only when Qt6 is available (top-level CMake guard).
# ---------------------------------------------------------------------------
add_executable(qt_bridge_demo main.cpp)
target_link_libraries(qt_bridge_demo PRIVATE crisp::qt_bridge)
