# ---------------------------------------------------------------------------
# crisp::event library
#
# STATIC library: non-template mechanisms live in .cpp (AGENTS.md §5.3);
# the templated dispatch machinery stays in headers/.inl. M1: sync core.
# M2: async core (CtQueue, CtEventLoop, routing).
# ---------------------------------------------------------------------------
add_library(crisp_event STATIC
    ct_dispatcher.cpp
    ct_event_loop.cpp
    ct_handler_token.cpp
    ct_queue.cpp
    ct_receiver.cpp
    ct_trace.cpp)

target_include_directories(crisp_event PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>)

target_compile_features(crisp_event PUBLIC cxx_std_17)

# Alias so consumers can link crisp::event
add_library(crisp::event ALIAS crisp_event)
