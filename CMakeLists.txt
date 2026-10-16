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

# Precompile stable headers by default. Unity grouping remains opt-in for local
# builds because single-file edits otherwise rebuild whole compilation batches.
option(WIZ8_PRECOMPILE_HEADERS "Precompile stable C++ dependencies" ON)
option(WIZ8_UNITY_BUILD "Compile game and renderer sources in batches" OFF)
option(WIZ8_SURRENDER_UNITY_BUILD "Unity-build SurRender independently of the game" OFF)
option(WIZ8_WARNINGS_AS_ERRORS "Fail first-party compilation on warnings (CI enables this)" OFF)
set(WIZ8_UNITY_BATCH_SIZE 4 CACHE STRING "Sources per unity compilation batch")
if(WIZ8_UNITY_BUILD AND NOT WIZ8_UNITY_BATCH_SIZE MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "WIZ8_UNITY_BATCH_SIZE must be a positive integer")
endif()

# First-party warning baseline.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()
# Designated initializers name only the fields they set; the rest keep their
# defaults (UiInputPolicy, ButtonSetup), so omitting fields is intended.
add_compile_options(-Wdeprecated -Wimplicit-fallthrough -Wno-unused-parameter
    $<$<COMPILE_LANGUAGE:CXX>:-Wsuggest-override>
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-missing-designated-field-initializers>)
# Check view bounds in Debug builds on libstdc++ versions that otherwise leave
# these checks disabled. This does not change the container ABI.
add_compile_definitions($<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>)

find_package(SDL3 3.4 CONFIG REQUIRED)
find_package(SDL3_image 3.4 CONFIG REQUIRED)
find_package(glm 1.0.3 CONFIG REQUIRED)
find_package(fmt 11.2.0 CONFIG REQUIRED)
find_package(Threads REQUIRED)

# Conan supplies SDL3, SDL3_image, GLM, fmt, FFmpeg, zlib, JPEG and glslang.
# The remaining fetched sources are hash- or commit-pinned until their
# exact upstream versions have ConanCenter recipes.
include(FetchContent)

# ConanCenter has SDL_mixer 3.2.0, but we require 3.2.4.
# The SDL release archive doesn't contain its optional vendored submodules.
# Build SDL_mixer static, matching Conan's static SDL3, so there is one
# SDL runtime and its audio device state is shared with the game.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build third-party dependencies statically")
# Use the built-in decoders (WAV, MP3, Vorbis, FLAC, MIDI) instead of
# introducing system codec dependencies.
set(SDLMIXER_VENDORED OFF CACHE BOOL "" FORCE)
set(SDLMIXER_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDLMIXER_TESTS OFF CACHE BOOL "" FORCE)
set(SDLMIXER_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLMIXER_STRICT ON CACHE BOOL "" FORCE)
set(SDLMIXER_FLAC_LIBFLAC OFF CACHE BOOL "" FORCE)
set(SDLMIXER_GME OFF CACHE BOOL "" FORCE)
set(SDLMIXER_MOD OFF CACHE BOOL "" FORCE)
set(SDLMIXER_MP3_MPG123 OFF CACHE BOOL "" FORCE)
set(SDLMIXER_MIDI_FLUIDSYNTH OFF CACHE BOOL "" FORCE)
set(SDLMIXER_OPUS OFF CACHE BOOL "" FORCE)
set(SDLMIXER_VORBIS_VORBISFILE OFF CACHE BOOL "" FORCE)
set(SDLMIXER_WAVPACK OFF CACHE BOOL "" FORCE)
FetchContent_Declare(sdl3_mixer
    URL https://github.com/libsdl-org/SDL_mixer/releases/download/release-3.2.4/SDL3_mixer-3.2.4.tar.gz
    URL_HASH SHA256=182a07c745375e113dc740d43964ff21b0be29f29f59876c4dbc4db3d32f6901)
FetchContent_MakeAvailable(sdl3_mixer)

# Suppress third-party warnings in our -Werror builds.
function(wiz8_system_headers)
    foreach(dependency IN LISTS ARGN)
        get_target_property(aliased ${dependency} ALIASED_TARGET)
        if(aliased)
            set(dependency ${aliased})
        endif()
        get_target_property(directories ${dependency} INTERFACE_INCLUDE_DIRECTORIES)
        set_target_properties(${dependency} PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${directories}")
    endforeach()
endfunction()
set(UTF8_TESTS OFF CACHE BOOL "" FORCE)
set(UTF8PROC_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(utf8cpp
    URL https://codeload.github.com/nemtrif/utfcpp/tar.gz/refs/tags/v4.0.6
    URL_HASH SHA256=6920a6a5d6a04b9a89b2a89af7132f8acefd46e0c2a7b190350539e9213816c0)
FetchContent_Declare(utf8proc
    URL https://codeload.github.com/JuliaStrings/utf8proc/tar.gz/refs/tags/v2.10.0
    URL_HASH SHA256=6f4f1b639daa6dca9f80bc5db1233e9cbaa31a67790887106160b33ef743f136)
FetchContent_MakeAvailable(utf8cpp utf8proc)
wiz8_system_headers(fmt::fmt utf8cpp utf8proc)

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
if(WIZ8_UNITY_BUILD AND BUILD_TESTING)
    message(FATAL_ERROR
        "Unity build requires BUILD_TESTING=OFF: focused tests provide overlapping "
        "symbols that rely on normal object-file archive boundaries")
endif()
if(BUILD_TESTING)
    target_compile_definitions(wiz8_filesystem PRIVATE WIZ8_FILE_FAULTS)
    target_include_directories(wiz8_filesystem PRIVATE "${PROJECT_SOURCE_DIR}/tests/native")
    # Tests run through Catch2; CTest retains the existing environment,
    # timeout, runtime-asset and fixture relationships.
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG 8b08d4d79514f45f7e4ce2a607ac9c94e920d1bb # v3.15.3
        GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(Catch2)
    wiz8_system_headers(Catch2::Catch2 Catch2::Catch2WithMain)
    include("${catch2_SOURCE_DIR}/extras/Catch.cmake")
    add_subdirectory(tests/portable)
endif()
find_package(ZLIB CONFIG REQUIRED)
find_package(FFMPEG CONFIG REQUIRED)

# Settings shared by every native component.
add_library(wiz8_native_settings INTERFACE)
target_compile_definitions(wiz8_native_settings INTERFACE NDEBUG GLM_ENABLE_EXPERIMENTAL)
# Keep the transitional source semantics with either Clang command-line frontend.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(WIZ8_COMPAT_OPTIONS
        "/FI${PROJECT_SOURCE_DIR}/include/wiz8/compat/compiler.h"
        /clang:-fsigned-char)
    set(WIZ8_GAME_OPTIONS
        ${WIZ8_COMPAT_OPTIONS} /clang:-fwrapv /clang:-fno-strict-aliasing)
else()
    set(WIZ8_COMPAT_OPTIONS
        "SHELL:-include ${PROJECT_SOURCE_DIR}/include/wiz8/compat/compiler.h"
        -fsigned-char)
    set(WIZ8_GAME_OPTIONS
        ${WIZ8_COMPAT_OPTIONS} -fwrapv -fno-strict-aliasing)
endif()
target_compile_options(wiz8_native_settings INTERFACE ${WIZ8_GAME_OPTIONS})
if(WIZ8_WARNINGS_AS_ERRORS)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(wiz8_native_settings INTERFACE /WX)
    else()
        target_compile_options(wiz8_native_settings INTERFACE -Werror)
    endif()
endif()
target_include_directories(wiz8_native_settings INTERFACE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/wiz8"
    "${PROJECT_SOURCE_DIR}/include/wiz8/engine_code"
    "${PROJECT_SOURCE_DIR}/src/sgp"
)
target_link_libraries(wiz8_native_settings INTERFACE glm::glm-header-only Threads::Threads ${CMAKE_DL_LIBS})

# Shared platform helpers and the SDL CPU surface backend used by SGP and SurRender.
add_library(wiz8_compat STATIC
    src/compat/native_helpers.cpp
    src/native/surface_storage.cpp
)
target_compile_options(wiz8_compat PRIVATE ${WIZ8_COMPAT_OPTIONS})
target_include_directories(wiz8_compat PRIVATE "${PROJECT_SOURCE_DIR}/include/wiz8" "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(wiz8_compat PUBLIC wiz8_filesystem SDL3::SDL3)
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
