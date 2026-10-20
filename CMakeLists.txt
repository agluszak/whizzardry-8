cmake_minimum_required(VERSION 4.4.4)
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
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# No C++ modules: avoid per-source scans and generated module response files
# so the compilation database is also immediately usable by analysis tools.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Precompile stable headers by default. Unity grouping remains opt-in for local
# builds because single-file edits otherwise rebuild whole compilation batches.
option(WIZ8_PRECOMPILE_HEADERS "Precompile stable C++ dependencies" ON)
option(WIZ8_UNITY_BUILD "Compile game and renderer sources in batches" OFF)
option(WIZ8_RENDER_UNITY_BUILD "Unity-build rendering independently of the game" OFF)
option(WIZ8_INTEGER_SANITIZER "Audit implicit integer truncation at data/image boundaries" OFF)
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
    $<$<COMPILE_LANGUAGE:CXX>:-Wshadow-field-in-constructor-modified>
    $<$<COMPILE_LANGUAGE:CXX>:-Wshorten-64-to-32>
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-missing-designated-field-initializers>)
# Clang 23's intra-procedural lifetime analysis: dangling and use-after-free.
add_compile_options(
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:Clang>,$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,23>>:-Wlifetime-safety-permissive>)
# Check standard-library bounds in development and sanitizer builds.
# This does not change the container ABI.
add_compile_definitions($<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:_GLIBCXX_ASSERTIONS>)

# Fail at configure time if the selected standard library lacks the C++26
# facilities used by the game (the language flag alone is insufficient).
include(CheckCXXSourceCompiles)
file(READ "${PROJECT_SOURCE_DIR}/cmake/Cxx26LibraryCheck.cpp" wiz8_cxx26_library_check)
check_cxx_source_compiles("${wiz8_cxx26_library_check}" WIZ8_CXX26_LIBRARY)
if(NOT WIZ8_CXX26_LIBRARY)
    message(FATAL_ERROR "Whizzardry requires C++26 function_ref and inplace_vector (Linux: libstdc++ 16 or newer)")
endif()

find_package(SDL3 3.4 CONFIG REQUIRED)
find_package(SDL3_image 3.4 CONFIG REQUIRED)
find_package(glm 1.0.3 CONFIG REQUIRED)
find_package(fmt 11.2.0 CONFIG REQUIRED)
find_package(Threads REQUIRED)
# Linux: libvulkan-dev; macOS/Windows: the Vulkan SDK (MoltenVK on macOS).
find_package(Vulkan REQUIRED)

# Conan supplies SDL3, SDL3_image, GLM, fmt, FFmpeg, zlib and glslang.
# The remaining fetched sources are hash- or commit-pinned until their
# exact upstream versions have ConanCenter recipes.
include(FetchContent)

# ConanCenter has SDL_mixer 3.2.0, but we require 3.2.4.
# The SDL release archive doesn't contain its optional vendored submodules.
# Build SDL_mixer static against Conan's shared SDL3, so the mixer and game
# use the same SDL runtime and audio device state.
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
set(UTF8PROC_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(utf8proc
    URL https://codeload.github.com/JuliaStrings/utf8proc/tar.gz/refs/tags/v2.10.0
    URL_HASH SHA256=6f4f1b639daa6dca9f80bc5db1233e9cbaa31a67790887106160b33ef743f136)
FetchContent_MakeAvailable(utf8proc)
wiz8_system_headers(fmt::fmt utf8proc)

# SDL owns platform windows and Vulkan surfaces. VSG owns the render graph,
# graphics resources and presentation. Wizardry shaders compile at build time.
set(VSG_SUPPORTS_Windowing OFF CACHE BOOL "" FORCE)
# Require runtime compilation so standard VSG subgraphs can request ordinary
# ShaderSet variants beyond the combinations shipped as embedded SPIR-V.
find_package(glslang CONFIG REQUIRED)
set(VSG_SUPPORTS_ShaderCompiler ON CACHE BOOL "" FORCE)
set(VSG_SUPPORTS_ShaderOptimizer OFF CACHE BOOL "" FORCE)
FetchContent_Declare(vsg
    GIT_REPOSITORY https://github.com/vsg-dev/VulkanSceneGraph.git
    GIT_TAG 77839c704362d832c027cc878e5496a14972e83d # v1.1.16
    EXCLUDE_FROM_ALL SYSTEM)
# Upstream has its own warning and language requirements. Avoid imposing the
# game's compiler diagnostics on the dependency while preserving them locally.
get_property(wiz8_compile_options DIRECTORY PROPERTY COMPILE_OPTIONS)
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")
set(CMAKE_CXX_STANDARD 17)
FetchContent_MakeAvailable(vsg)
set(CMAKE_CXX_STANDARD 26)
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "${wiz8_compile_options}")

# No forced CRT vocabulary or short-wchar options at the portable API boundary.
add_library(wiz8_filesystem STATIC
    src/platform/asset_paths.cpp
    src/platform/filesystem.cpp
    src/platform/file_time.cpp
    src/platform/unicode.cpp
)
target_include_directories(wiz8_filesystem PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(wiz8_filesystem PUBLIC SDL3::SDL3 Threads::Threads fmt::fmt utf8proc)
target_compile_features(wiz8_filesystem PUBLIC cxx_std_26)
# SLF knowledge is confined to installation tooling, never linked into the game.
add_library(wiz8_asset_preparation STATIC tools/assets/prepare.cpp)
target_include_directories(wiz8_asset_preparation PUBLIC "${PROJECT_SOURCE_DIR}/tools/assets")
target_link_libraries(wiz8_asset_preparation PUBLIC wiz8_filesystem)
add_executable(wiz8_prepare_assets tools/assets/main.cpp)
target_link_libraries(wiz8_prepare_assets PRIVATE wiz8_asset_preparation)

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
target_compile_definitions(wiz8_native_settings INTERFACE GLM_ENABLE_EXPERIMENTAL)
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
)
target_link_libraries(wiz8_native_settings INTERFACE glm::glm-header-only Threads::Threads ${CMAKE_DL_LIBS}
    wiz8_filesystem SDL3::SDL3)

# The game clock, the presentation random stream and the SDL CPU surface backend.
add_library(wiz8_platform STATIC
    src/platform/random.cpp
    src/platform/timer.cpp
    src/native/surface_storage.cpp
)
target_link_libraries(wiz8_platform PUBLIC wiz8_native_settings)

# The architecture's lower layers: game data and rules. They build without the
# platform library, SDL or the engine, so the linker enforces what
# tools/check_layers.py checks in their includes.
add_library(wiz8_rules STATIC
    src/data/character_tables.cpp
    src/rules/action_choices.cpp
    src/rules/character_derivation.cpp
    src/rules/character_stats.cpp
    src/rules/events.cpp
    src/rules/inventory.cpp
    src/rules/item_rules.cpp
    src/rules/party.cpp
    src/rules/party_formation.cpp
    src/rules/party_stats.cpp
)
target_include_directories(wiz8_rules PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(wiz8_rules PUBLIC glm::glm-header-only fmt::fmt)
if(WIZ8_WARNINGS_AS_ERRORS)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(wiz8_rules PRIVATE /WX)
    else()
        target_compile_options(wiz8_rules PRIVATE -Werror)
    endif()
endif()

add_library(wiz8_engine STATIC)
target_link_libraries(wiz8_engine PUBLIC wiz8_rules)
add_subdirectory(src/render)
add_subdirectory(src/wiz8)

add_executable(Wiz8Native src/platform/main.cpp)
target_link_libraries(Wiz8Native PRIVATE wiz8_engine SDL3::SDL3)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Keep the noisy integer experiment at file and pixel boundaries. Gameplay
# arithmetic has intentional narrowing and is covered by the normal UBSan run.
if(WIZ8_INTEGER_SANITIZER)
    # Every first-party executable links the platform library. Supply the
    # runtime even when the option is enabled outside the sanitizer preset.
    target_link_options(wiz8_filesystem INTERFACE -fsanitize=implicit-integer-truncation)
    foreach(component wiz8_engine wiz8_filesystem wiz8_asset_preparation)
        get_target_property(component_dir ${component} SOURCE_DIR)
        get_target_property(component_sources ${component} SOURCES)
        foreach(source IN LISTS component_sources)
            string(TOLOWER "${source}" lower_source)
            if(lower_source MATCHES "(image|pixel|blit|retail|chunk|loadsave|packed|mesh_loader|level_loader|npc_script|unicode|assets/prepare).*\\.cpp$")
                cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${component_dir}" OUTPUT_VARIABLE absolute_source)
                set_property(SOURCE "${absolute_source}" TARGET_DIRECTORY ${component}
                    PROPERTY SKIP_PRECOMPILE_HEADERS ON)
                set_property(SOURCE "${absolute_source}" TARGET_DIRECTORY ${component}
                    APPEND PROPERTY COMPILE_OPTIONS -fsanitize=implicit-integer-truncation
                    -fno-sanitize-recover=implicit-integer-truncation)
            endif()
        endforeach()
    endforeach()
endif()
