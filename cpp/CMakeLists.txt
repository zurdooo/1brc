cmake_minimum_required(VERSION 3.25)
project(1brc CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Conan-generated toolchain sets up find_package paths; consumed via -DCMAKE_TOOLCHAIN_FILE
# when configuring: cmake -B build -DCMAKE_TOOLCHAIN_FILE=build/Release/generators/conan_toolchain.cmake

# ── Build type default ────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── Dependencies ─────────────────────────────────────────────────────────────
# List only the Boost components you actually use, e.g.:
# find_package(Boost REQUIRED COMPONENTS program_options filesystem)
# For header-only usage (e.g. boost/algorithm, boost/container) no component needed:
find_package(Boost REQUIRED CONFIG)

# ── Executable ───────────────────────────────────────────────────────────────
add_executable(1brc src/1brc.cpp)

# Link Boost — header-only (Boost::boost) is always available.
# Add compiled components after Boost::boost as needed, e.g. Boost::program_options
target_link_libraries(1brc PRIVATE Boost::boost)

target_compile_options(1brc PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
    -Wall -Wextra -Wpedantic
    -O0 -g

    # $<$<CONFIG:Release>:-O3 -march=native>
    # $<$<CONFIG:Debug>:-g -O0>
    >
)

# ── Install ──────────────────────────────────────────────────────────────────
install(TARGETS 1brc RUNTIME DESTINATION bin)
