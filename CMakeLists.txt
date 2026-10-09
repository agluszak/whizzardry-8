cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration")
endif()
project(whizzardry8 LANGUAGES C CXX)

# The native lane builds for Linux and macOS with Clang. The legacy lane
# reproduces the shipped 32-bit Windows binaries with VC6 or clang-cl.
if(NOT MSVC)
    include(cmake/Native.cmake)
    return()
endif()
if(NOT MSVC_VERSION EQUAL 1200 AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Use Microsoft Visual C++ 6.0 or clang-cl targeting 32-bit Windows")
endif()
enable_language(RC)
set(CMAKE_MSVC_RUNTIME_LIBRARY "")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

set(ZLIB_SOURCE "" CACHE PATH "zlib 1.0.4 source directory")
if(NOT IS_DIRECTORY "${ZLIB_SOURCE}")
    message(FATAL_ERROR "Set ZLIB_SOURCE to the zlib 1.0.4 source directory")
endif()

include(cmake/CompileSettings.cmake)
include(cmake/ProductSupport.cmake)
add_subdirectory(src/sgp)
add_subdirectory(src/surrender)
add_subdirectory(src/wiz8)

# Optional oracle harness; production legacy targets keep their original code.
option(WIZ8_BUILD_LEGACY_BLITTER_TEST "Build the assembly blitter capture harness" OFF)
if(WIZ8_BUILD_LEGACY_BLITTER_TEST)
    add_executable(legacy_blitter_test tests/native/blitter_test.cpp)
    target_link_libraries(legacy_blitter_test PRIVATE WIZ8_SGP wiz8_compile_settings)
endif()
