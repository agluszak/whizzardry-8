# Compiler settings shared by the game and SurRender components.
add_library(wiz8_compile_settings INTERFACE)
target_compile_options(wiz8_compile_settings INTERFACE
    /nologo /Z7
    "/FI${CMAKE_CURRENT_SOURCE_DIR}/include/wiz8/compat/compiler.h"
    /O2
    /MD
    /GR-
    "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/G6>"
    "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/G6>"
)
target_compile_definitions(wiz8_compile_settings INTERFACE
    NDEBUG NOMINMAX WIN32_LEAN_AND_MEAN
)
target_include_directories(wiz8_compile_settings INTERFACE
    include
    include/wiz8
    include/wiz8/engine_code
    src/sgp
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(wiz8_compile_settings INTERFACE /Zc:wchar_t- $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
    target_compile_definitions(wiz8_compile_settings INTERFACE _CRT_SECURE_NO_WARNINGS WIZ8_CLANG_LINT)
endif()
