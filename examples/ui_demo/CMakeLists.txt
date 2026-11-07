# ---------------------------------------------------------------------------
# UI subsystem demo: shows CtWidgetBase / CtPushButton / CtLabel / CtLineEdit
# / CtBoxLayout / CtFocusChain / CtPainter (off-screen)
# ---------------------------------------------------------------------------
add_executable(ui_demo main.cpp)
target_link_libraries(ui_demo PRIVATE crisp::ui)
