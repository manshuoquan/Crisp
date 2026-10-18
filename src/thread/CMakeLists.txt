# ---------------------------------------------------------------------------
# crisp::thread library (MT1)
#
# STATIC library: non-template mechanisms live in .cpp (AGENTS.md §5.3);
# the templated task/handle machinery stays in headers. Pure C++17, zero Qt
# dependency. Implements docs/threading/03-rewrite-proposal.md MT1.
# ---------------------------------------------------------------------------
add_library(crisp_thread STATIC
    ct_thread.cpp
    ct_task_base.cpp
    ct_task_queue.cpp
    ct_thread_pool.cpp
    ct_thread_trace.cpp)

target_include_directories(crisp_thread PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>)

target_compile_features(crisp_thread PUBLIC cxx_std_17)

# Alias so consumers can link crisp::thread
add_library(crisp::thread ALIAS crisp_thread)
