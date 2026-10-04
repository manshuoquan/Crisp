# ---------------------------------------------------------------------------
# Unit tests for crisp::event (hand-written test doubles, NO mock framework)
# ---------------------------------------------------------------------------
add_executable(event_tests
    main.cpp
    test_ct_event.cpp
    test_ct_dispatcher.cpp
    test_ct_queue.cpp
    tu_a.cpp
    tu_b.cpp)

target_link_libraries(event_tests PRIVATE crisp::event)

add_test(NAME event_tests COMMAND event_tests)
