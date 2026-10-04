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

# ---------------------------------------------------------------------------
# Tests & demos
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests/event)
add_subdirectory(examples/event_demo)
