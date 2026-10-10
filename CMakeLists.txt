cmake_minimum_required(VERSION 3.21)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration")
endif()
project(whizzardry8 LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin|Windows)$" OR
   NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Whizzardry requires 64-bit Linux, macOS or Windows")
endif()
if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang" OR
   NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Whizzardry requires native Clang")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# First-party warning baseline.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()
add_compile_options(-Wdeprecated -Wimplicit-fallthrough -Wno-unused-parameter
    $<$<COMPILE_LANGUAGE:CXX>:-Wsuggest-override>)
# Check view bounds in Debug builds on libstdc++ versions that otherwise leave
# these checks disabled. This does not change the container ABI.
add_compile_definitions($<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>)

find_package(SDL3 3.4 CONFIG REQUIRED)
find_package(SDL3_image 3.4 CONFIG REQUIRED)
find_package(SDL3_mixer 3.2.4 CONFIG REQUIRED)
find_package(Threads REQUIRED)

# GLM's headers are shared by game calculations and renderer storage adapters.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
include(FetchContent)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
    URL https://codeload.github.com/g-truc/glm/tar.gz/refs/tags/1.0.3
    URL_HASH SHA256=6775e47231a446fd086d660ecc18bcd076531cfedd912fbd66e576b118607001
)
FetchContent_MakeAvailable(glm)
# Native text facilities: typed runtime printf formatting, Unicode conversion,
# and Unicode grapheme boundaries. Pin downloads to reproducible releases.
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(UTF8_TESTS OFF CACHE BOOL "" FORCE)
set(UTF8PROC_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(fmt
    URL https://codeload.github.com/fmtlib/fmt/tar.gz/refs/tags/11.2.0
    URL_HASH SHA256=bc23066d87ab3168f27cef3e97d545fa63314f5c79df5ea444d41d56f962c6af)
FetchContent_Declare(utf8cpp
    URL https://codeload.github.com/nemtrif/utfcpp/tar.gz/refs/tags/v4.0.6
    URL_HASH SHA256=6920a6a5d6a04b9a89b2a89af7132f8acefd46e0c2a7b190350539e9213816c0)
FetchContent_Declare(utf8proc
    URL https://codeload.github.com/JuliaStrings/utf8proc/tar.gz/refs/tags/v2.10.0
    URL_HASH SHA256=6f4f1b639daa6dca9f80bc5db1233e9cbaa31a67790887106160b33ef743f136)
FetchContent_MakeAvailable(fmt utf8cpp utf8proc)

# No forced CRT vocabulary or short-wchar options at the portable API boundary.
add_library(wiz8_filesystem STATIC
    src/platform/path_resolver.cpp
    src/platform/filesystem.cpp
    src/platform/slf.cpp
    src/platform/file_time.cpp
    src/platform/unicode.cpp
)
target_include_directories(wiz8_filesystem PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(wiz8_filesystem PUBLIC SDL3::SDL3 Threads::Threads fmt::fmt utf8cpp utf8proc)
target_compile_features(wiz8_filesystem PUBLIC cxx_std_23)
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests/portable)
endif()
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec
    libavformat
    libavutil
    libswresample
    libswscale
)

# Settings shared by every native component.
add_library(wiz8_native_settings INTERFACE)
target_compile_definitions(wiz8_native_settings INTERFACE NDEBUG GLM_ENABLE_EXPERIMENTAL)
# Keep the transitional source semantics with either Clang command-line frontend.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(WIZ8_COMPAT_OPTIONS
        "/FI${PROJECT_SOURCE_DIR}/include/wiz8/compat/compiler.h"
        /clang:-fsigned-char)
    set(WIZ8_GAME_OPTIONS
        ${WIZ8_COMPAT_OPTIONS} /clang:-fwrapv /clang:-fno-strict-aliasing
        $<$<COMPILE_LANGUAGE:CXX>:/GR->)
else()
    set(WIZ8_COMPAT_OPTIONS
        "SHELL:-include ${PROJECT_SOURCE_DIR}/include/wiz8/compat/compiler.h"
        -fsigned-char)
    set(WIZ8_GAME_OPTIONS
        ${WIZ8_COMPAT_OPTIONS} -fwrapv -fno-strict-aliasing
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
endif()
target_compile_options(wiz8_native_settings INTERFACE ${WIZ8_GAME_OPTIONS})
target_include_directories(wiz8_native_settings INTERFACE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/wiz8"
    "${PROJECT_SOURCE_DIR}/include/wiz8/engine_code"
    "${PROJECT_SOURCE_DIR}/src/sgp"
)
target_link_libraries(wiz8_native_settings INTERFACE glm::glm-header-only Threads::Threads ${CMAKE_DL_LIBS})

# Remaining narrow CRT spelling helpers; Unicode lives at format boundaries.
add_library(wiz8_compat STATIC
    src/compat/native_helpers.cpp
)
target_compile_options(wiz8_compat PRIVATE ${WIZ8_COMPAT_OPTIONS})
target_include_directories(wiz8_compat PRIVATE "${PROJECT_SOURCE_DIR}/include/wiz8" "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(wiz8_compat PUBLIC wiz8_filesystem)
if(UNIX AND NOT APPLE)
    target_link_options(wiz8_compat PRIVATE -Wl,--no-undefined)
endif()
target_link_libraries(wiz8_native_settings INTERFACE wiz8_compat)

add_subdirectory(src/surrender)
add_subdirectory(src/wiz8)

add_executable(Wiz8Native src/sgp/native/main.cpp)
target_link_libraries(Wiz8Native PRIVATE WIZ8_GAME_CORE SDL3::SDL3)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
