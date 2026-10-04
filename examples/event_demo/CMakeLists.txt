# ---------------------------------------------------------------------------
# Event system demo: shows CtEvent / CtDispatcher / filters / observers / trace
# ---------------------------------------------------------------------------
add_executable(event_demo main.cpp)
target_link_libraries(event_demo PRIVATE crisp::event)
