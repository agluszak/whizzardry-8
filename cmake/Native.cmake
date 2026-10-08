# Native Linux/macOS lane: modern Clang, system libraries, no Windows SDK.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "The native lane requires Clang")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The native lane targets 64-bit Linux and macOS")
endif()

set(WIZ8_NATIVE ON)
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

# Microsoft CRT extensions and two-byte wide strings (compat/native.h).
add_library(wiz8_compat STATIC src/compat/native_crt.cpp)
target_compile_options(wiz8_compat PRIVATE
    "SHELL:-include ${PROJECT_SOURCE_DIR}/include/wiz8/compat/compiler.h"
    -fsigned-char -fshort-wchar -fno-builtin-wcslen)
target_compile_definitions(wiz8_compat PRIVATE WIZ8_NATIVE)
target_include_directories(wiz8_compat PRIVATE "${PROJECT_SOURCE_DIR}/include/wiz8")
target_link_libraries(wiz8_native_settings INTERFACE wiz8_compat)

add_subdirectory(src/surrender)
add_subdirectory(src/sgp)

enable_testing()
add_executable(native_compression_test tests/native/compression_test.cpp)
target_link_libraries(native_compression_test PRIVATE WIZ8_SGP)
add_test(NAME native_compression COMMAND native_compression_test)

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
    COMMAND sh -c "! nm -u $<TARGET_FILE:SURRENDER> $<TARGET_FILE:native_crt_test> | grep -E ' U (wcs|wmem|swprintf|vswprintf|wprintf)'")

# srGERD on the SDL3 GPU device; needs a display and a Vulkan driver.
add_executable(srdd_spike tests/native/srdd_spike.cpp)
target_link_libraries(srdd_spike PRIVATE SURRENDER wiz8_native_settings SDL3::SDL3)
add_test(NAME srdd_spike COMMAND srdd_spike 60)
