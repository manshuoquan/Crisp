cmake_minimum_required(VERSION 3.20)

project(CRISP VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# MinGW 8.1 is a 32-bit compiler; high parallelism may exhaust memory (OOM).
# We cap parallel jobs at 2 for safety (see AGENTS.md -> build & test).
set(CMAKE_BUILD_PARALLEL_LEVEL 2 CACHE STRING "Max parallel build jobs" FORCE)

add_compile_options(-Wall -Wextra)

# ---------------------------------------------------------------------------
# Libraries
# ---------------------------------------------------------------------------
add_subdirectory(src/event)
add_subdirectory(src/thread)

# Qt bridge (M3/M4): built only when Qt6 is found. The bridge directory runs its
# own find_package(Qt6 COMPONENTS Core Gui QUIET); the top-level guard keeps the
# core library + tests fully Qt-independent (docs/event-system/05-qt-bridge.md
# §5.2 contract #1). Probe Qt6 ONCE here so the guard below can decide.
find_package(Qt6 COMPONENTS Core Gui QUIET)
if(Qt6_FOUND)
    add_subdirectory(src/bridge)
endif()

# ---------------------------------------------------------------------------
# Tests & demos
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests/event)
add_subdirectory(tests/thread)
if(Qt6_FOUND)
    add_subdirectory(tests/bridge)
endif()
add_subdirectory(examples/event_demo)
