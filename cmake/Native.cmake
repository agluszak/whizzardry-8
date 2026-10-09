# Native Linux/macOS lane: modern Clang, system libraries, no Windows SDK.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "The native lane requires Clang")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The native lane targets 64-bit Linux and macOS")
endif()

set(WIZ8_NATIVE ON)
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
find_package(SDL3 CONFIG REQUIRED)
find_package(Threads REQUIRED)

# Settings shared by every native component.
add_library(wiz8_native_settings INTERFACE)
target_compile_definitions(wiz8_native_settings INTERFACE WIZ8_NATIVE NDEBUG)
target_compile_options(wiz8_native_settings INTERFACE
    "SHELL:-include ${PROJECT_SOURCE_DIR}/include/wiz8/compat/compiler.h"
    # MSVC semantics the recovered code relies on: signed char on every
    # architecture, wrapping integer arithmetic and type punning through
    # pointer casts.
    -fsigned-char -fwrapv -fno-strict-aliasing
    # Two-byte wchar_t as on Windows; compat/native.h routes wide-string calls
    # to implementations for that width.
    -fshort-wchar
    # LLVM rewrites wide strlen loops into wcslen calls, and glibc's wcslen
    # reads four-byte characters.
    -fno-builtin-wcslen
    # Retail was built without RTTI (/GR-); several interfaces have no key function.
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)
target_include_directories(wiz8_native_settings INTERFACE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/wiz8"
    "${PROJECT_SOURCE_DIR}/include/wiz8/engine_code"
    "${PROJECT_SOURCE_DIR}/src/sgp"
)
target_link_libraries(wiz8_native_settings INTERFACE Threads::Threads ${CMAKE_DL_LIBS})

# One process-wide owner for roots, file handles and CRT compatibility. Sharing
# this library avoids separate overlay state in the game and SurRender on macOS.
add_library(wiz8_compat SHARED
    src/compat/native_crt.cpp
    src/compat/platform_paths.cpp
    src/compat/platform_crt.cpp
    src/compat/platform_files.cpp
    src/compat/platform_system.cpp
    src/compat/platform_events.cpp
)
target_compile_options(wiz8_compat PRIVATE
    "SHELL:-include ${PROJECT_SOURCE_DIR}/include/wiz8/compat/compiler.h"
    -fsigned-char -fshort-wchar -fno-builtin-wcslen)
target_compile_definitions(wiz8_compat PRIVATE WIZ8_NATIVE)
target_include_directories(wiz8_compat PRIVATE "${PROJECT_SOURCE_DIR}/include/wiz8")
target_link_libraries(wiz8_compat PRIVATE Threads::Threads SDL3::SDL3)
if(NOT APPLE)
    target_link_options(wiz8_compat PRIVATE -Wl,--no-undefined)
endif()
target_include_directories(wiz8_native_settings INTERFACE "${PROJECT_SOURCE_DIR}/src/compat")
target_link_libraries(wiz8_native_settings INTERFACE wiz8_compat)

add_subdirectory(src/surrender)
add_subdirectory(src/sgp)
include("${PROJECT_SOURCE_DIR}/src/wiz8/native.cmake")

# Compile the real entry point while audio/video ports still block final linking.
add_library(WIZ8_NATIVE_SHELL OBJECT src/sgp/native/main.cpp)
target_link_libraries(WIZ8_NATIVE_SHELL PRIVATE wiz8_native_settings SDL3::SDL3)

enable_testing()
add_executable(native_compression_test tests/native/compression_test.cpp)
target_link_libraries(native_compression_test PRIVATE WIZ8_SGP)
add_test(NAME native_compression COMMAND native_compression_test)

add_executable(native_files_test tests/native/files_test.cpp)
target_link_libraries(native_files_test PRIVATE WIZ8_SGP SURRENDER)
add_test(NAME native_files COMMAND native_files_test)

add_executable(native_events_test tests/native/events_test.cpp)
target_link_libraries(native_events_test PRIVATE WIZ8_SGP SDL3::SDL3)
add_test(NAME native_events COMMAND native_events_test)
set_tests_properties(native_events PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy" TIMEOUT 10)

add_executable(native_imports_test tests/native/imports_test.cpp)
target_link_libraries(native_imports_test PRIVATE SURRENDER wiz8_native_settings)
add_test(NAME native_imports COMMAND native_imports_test)

add_executable(native_crt_test tests/native/crt_test.cpp)
target_link_libraries(native_crt_test PRIVATE wiz8_native_settings)
add_test(NAME native_crt COMMAND native_crt_test)

add_executable(native_pointer_test tests/native/pointer_test.cpp)
target_link_libraries(native_pointer_test PRIVATE wiz8_native_settings)
add_test(NAME native_pointer COMMAND native_pointer_test)

add_executable(native_blitter_test tests/native/blitter_test.cpp)
target_link_libraries(native_blitter_test PRIVATE WIZ8_SGP)
add_test(NAME native_blitters COMMAND native_blitter_test
    "${PROJECT_SOURCE_DIR}/tests/native/blitters_legacy.txt")

# glibc's wide-string functions assume four-byte wchar_t; no native binary
# may import them.
add_test(NAME native_no_glibc_wide_strings
    COMMAND sh -c "! nm -u $<TARGET_FILE:wiz8_compat> $<TARGET_FILE:SURRENDER> $<TARGET_FILE:native_crt_test> | grep -E ' U (wcs|wmem|swprintf|vswprintf|wprintf)'")

# srGERD on the SDL3 GPU device; needs a display and a Vulkan driver.
add_executable(srdd_spike tests/native/srdd_spike.cpp)
target_link_libraries(srdd_spike PRIVATE SURRENDER wiz8_native_settings SDL3::SDL3)
add_test(NAME srdd_spike COMMAND srdd_spike 60)

add_executable(native_surface_oracle tests/native/surface_oracle.cpp)
target_link_libraries(native_surface_oracle PRIVATE WIZ8_SGP)
add_test(NAME native_surface_oracle COMMAND native_surface_oracle "${PROJECT_SOURCE_DIR}/tests/native/surfaces_legacy.txt")

add_executable(native_game_graphics tests/native/game_graphics.cpp)
target_compile_definitions(native_game_graphics PRIVATE WIZ8_RENDERER_LIBRARY="$<TARGET_FILE:SURRENDER>")
target_link_libraries(native_game_graphics PRIVATE WIZ8_GAME_CORE WIZ8_SGP WIZ8_GAME_CORE SDL3::SDL3)
if(APPLE)
    target_link_options(native_game_graphics PRIVATE -Wl,-dead_strip)
else()
    # Discard unused ASan global-registration sections alongside unused game
    # functions; the harness exercises graphics before the media link exists.
    target_link_options(native_game_graphics PRIVATE -Wl,--gc-sections -Wl,-z,start-stop-gc)
endif()
# Uses installed retail assets, supplied through WIZ8_ASSET_ROOT; run explicitly.

add_executable(native_audio_test tests/native/audio_test.cpp)
target_link_libraries(native_audio_test PRIVATE WIZ8_SGP)
target_compile_definitions(native_audio_test PRIVATE WIZ8_AUDIO_TEST_MP3="${PROJECT_SOURCE_DIR}/tests/native/tone.mp3")
add_test(NAME native_audio COMMAND native_audio_test)

# Standalone SDL/Vulkan lifetime control for diagnosing host leak reports.
# Run manually: failures here are independent of game and SurRender ownership.
add_executable(native_gpu_lifecycle tests/native/gpu_lifecycle.cpp)
target_link_libraries(native_gpu_lifecycle PRIVATE SDL3::SDL3)
