cmake_minimum_required(VERSION 3.16)
project(TempleTrap LANGUAGES CXX)

# ─── C++ standard ─────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Default to Release build ─────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ─── Compiler flags ───────────────────────────────────────────────────────────
add_compile_options(
  $<$<CONFIG:Release>:-O3>
  $<$<CONFIG:Release>:-DNDEBUG>
)

# Native-arch optimisation (improves SIMD/hash throughput on the build machine).
# Disable when cross-compiling or building portable binaries.
option(TEMPLE_NATIVE_ARCH "Optimise for the build machine's CPU" ON)
if(TEMPLE_NATIVE_ARCH AND NOT MSVC)
  add_compile_options($<$<CONFIG:Release>:-march=native>)
endif()

# ─── Link-Time Optimisation ───────────────────────────────────────────────────
include(CheckIPOSupported)
check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT LTO_MSG)
if(LTO_SUPPORTED)
  message(STATUS "LTO enabled")
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
else()
  message(STATUS "LTO not supported: ${LTO_MSG}")
endif()

# ─── SFML ─────────────────────────────────────────────────────────────────────
# Prefer a system-installed SFML 2.x.  If not found automatically, set
# SFML_DIR to the directory containing SFMLConfig.cmake:
#   cmake -DSFML_DIR=/path/to/sfml/lib/cmake/SFML ..
find_package(SFML 2.5 COMPONENTS graphics window system QUIET)

if(NOT SFML_FOUND)
  message(STATUS "System SFML not found — trying FetchContent")
  include(FetchContent)
  FetchContent_Declare(
    SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG        2.6.1
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(SFML)
endif()

# ─── temple_solver ────────────────────────────────────────────────────────────
add_executable(temple_solver
  solver.cpp
)

target_include_directories(temple_solver PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(temple_solver PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ─── temple_visualizer ────────────────────────────────────────────────────────
add_executable(temple_visualizer
  visualizer.cpp
)

target_include_directories(temple_visualizer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(temple_visualizer PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

target_link_libraries(temple_visualizer PRIVATE
  sfml-graphics
  sfml-window
  sfml-system
)

# ─── Install targets ──────────────────────────────────────────────────────────
install(TARGETS temple_solver temple_visualizer
  RUNTIME DESTINATION bin
)
