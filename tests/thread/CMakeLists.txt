# ---------------------------------------------------------------------------
# Unit tests for crisp::thread (MT1; hand-written test doubles, no mock)
# ---------------------------------------------------------------------------
add_executable(thread_tests
    main.cpp
    test_ct_thread_pool.cpp
    test_ct_future_bridge.cpp)

# Reuse the event test harness header (project rule: hand-written, no mock).
target_include_directories(thread_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../event)

target_link_libraries(thread_tests PRIVATE crisp::thread crisp::event)

add_test(NAME thread_tests COMMAND thread_tests)
