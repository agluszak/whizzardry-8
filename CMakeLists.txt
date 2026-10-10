cmake_minimum_required(VERSION 3.21)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration")
endif()
include(cmake/BootstrapVcpkg.cmake)
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
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(ZLIB REQUIRED)
find_package(SDL3 3.2 CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)
find_package(Threads REQUIRED)

# Audio dependency; do not build upstream examples or extra nodes.
include(FetchContent)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(MINIAUDIO_BUILD_EXAMPLES OFF)
set(MINIAUDIO_BUILD_TESTS OFF)
set(MINIAUDIO_BUILD_TOOLS OFF)
set(MINIAUDIO_INSTALL OFF)
set(MINIAUDIO_NO_EXTRA_NODES ON)
set(MINIAUDIO_NO_LIBVORBIS ON)
set(MINIAUDIO_NO_LIBOPUS ON)
FetchContent_Declare(miniaudio
    URL https://codeload.github.com/mackron/miniaudio/tar.gz/9634bedb5b5a2ca38c1ee7108a9358a4e233f14d
)
FetchContent_MakeAvailable(miniaudio)
target_link_libraries(miniaudio PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE)
    target_link_libraries(miniaudio PRIVATE m)
endif()

# Settings shared by every native component.
add_library(wiz8_native_settings INTERFACE)
target_compile_definitions(wiz8_native_settings INTERFACE NDEBUG)
# Keep the transitional source semantics with either Clang command-line frontend.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(WIZ8_COMPAT_OPTIONS
        "/FI${PROJECT_SOURCE_DIR}/include/wiz8/compat/compiler.h"
        /clang:-fsigned-char /clang:-fno-builtin-wcslen)
    set(WIZ8_GAME_OPTIONS
        ${WIZ8_COMPAT_OPTIONS} /clang:-fwrapv /clang:-fno-strict-aliasing
        $<$<COMPILE_LANGUAGE:CXX>:/GR->)
else()
    set(WIZ8_COMPAT_OPTIONS
        "SHELL:-include ${PROJECT_SOURCE_DIR}/include/wiz8/compat/compiler.h"
        -fsigned-char -fshort-wchar -fno-builtin-wcslen)
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
target_link_libraries(wiz8_native_settings INTERFACE Threads::Threads ${CMAKE_DL_LIBS})

# One process-wide owner for roots, file handles, CRT and SDL state in the executable.
add_library(wiz8_compat STATIC
    src/compat/native_crt.cpp
    src/compat/platform_paths.cpp
    src/compat/platform_crt.cpp
    src/compat/platform_files.cpp
    src/compat/platform_system.cpp
)
target_compile_options(wiz8_compat PRIVATE ${WIZ8_COMPAT_OPTIONS})
target_include_directories(wiz8_compat PRIVATE "${PROJECT_SOURCE_DIR}/include/wiz8")
target_link_libraries(wiz8_compat PRIVATE Threads::Threads SDL3::SDL3)
if(WIN32)
    # Keep host SDK declarations out of the transitional game vocabulary.
    add_library(wiz8_os STATIC src/compat/platform_fs_windows.cpp)
    target_link_libraries(wiz8_compat PRIVATE wiz8_os)
endif()
if(UNIX AND NOT APPLE)
    target_link_options(wiz8_compat PRIVATE -Wl,--no-undefined)
endif()
target_include_directories(wiz8_native_settings INTERFACE "${PROJECT_SOURCE_DIR}/src/compat")
target_link_libraries(wiz8_native_settings INTERFACE wiz8_compat)

add_subdirectory(src/surrender)
add_subdirectory(src/sgp)
add_subdirectory(src/wiz8)

add_executable(Wiz8Native src/sgp/native/main.cpp)
target_link_libraries(Wiz8Native PRIVATE WIZ8_GAME_CORE WIZ8_SGP WIZ8_GAME_CORE SDL3::SDL3)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
