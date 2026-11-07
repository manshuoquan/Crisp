# ---------------------------------------------------------------------------
# Threading subsystem demo: shows CtThreadPool / CtFuture / CtParallel /
# CtTaskEventBridge
# ---------------------------------------------------------------------------
add_executable(thread_demo main.cpp)
target_link_libraries(thread_demo PRIVATE crisp::thread crisp::event)
