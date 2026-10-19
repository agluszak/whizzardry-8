cmake_minimum_required(VERSION 4.4.4)
project(WhizzardryShaderTools LANGUAGES C CXX)
# This standalone regeneration tool has a canonical Linux x86-64 host.
# Normal macOS/Windows game builds use the checked-in offline outputs.
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    message(FATAL_ERROR "Shader regeneration requires a Linux x86-64 host")
endif()
include(FetchContent)
find_package(SDL3 CONFIG REQUIRED)
# Conan's SDL CMake target names differ from the upstream static target.
if(NOT TARGET SDL3::SDL3-static)
    add_library(SDL3::SDL3-static INTERFACE IMPORTED)
    set_property(TARGET SDL3::SDL3-static PROPERTY INTERFACE_LINK_LIBRARIES SDL3::SDL3)
endif()
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spirv_cross
    URL https://github.com/KhronosGroup/SPIRV-Cross/archive/refs/tags/vulkan-sdk-1.4.313.0.tar.gz
    URL_HASH SHA256=7d1de24918bea9897753f7561d4d154f68ec89c36bb70c13598222b8039d4212)
FetchContent_MakeAvailable(spirv_cross)
add_library(spirv_cross_c ALIAS spirv-cross-c)
# Prebuilt DXC is a build-time tool, never a game dependency. Pin the same
# Linux SDK archive as SDL_shadercross's own download script.
FetchContent_Declare(dxc
    URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2602/linux_dxc_2026_02_20.x86_64.tar.gz
    URL_HASH SHA256=a1d3e3b5e1c5685b3eb27d5e8890e41d87df45def05112a2d6f1a63a931f7d60)
FetchContent_MakeAvailable(dxc)
set(DirectXShaderCompiler_INCLUDE_PATH "${dxc_SOURCE_DIR}/include/dxc" CACHE PATH "" FORCE)
set(DirectXShaderCompiler_dxcompiler_LIBRARY "${dxc_SOURCE_DIR}/lib/libdxcompiler.so" CACHE FILEPATH "" FORCE)
set(DirectXShaderCompiler_dxil_LIBRARY "${dxc_SOURCE_DIR}/lib/libdxil.so" CACHE FILEPATH "" FORCE)
set(SDLSHADERCROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_STATIC ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_CLI_STATIC ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_SPIRVCROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(shadercross
    URL https://github.com/libsdl-org/SDL_shadercross/archive/1ff05bec573988a98ef9e0260b4da44f512b8367.tar.gz
    URL_HASH SHA256=60aae35ff71bd1d5f85b363e9d077a86bb27dd36333c42685e56c2fd541174d2)
FetchContent_MakeAvailable(shadercross)
