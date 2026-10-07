set(ZLIB_SOURCE "" CACHE PATH "zlib 1.0.4 source directory")

file(GLOB WIZ8_ZLIB_UNITS CONFIGURE_DEPENDS "${ZLIB_SOURCE}/*.c")
list(REMOVE_ITEM WIZ8_ZLIB_UNITS
    "${ZLIB_SOURCE}/example.c" "${ZLIB_SOURCE}/minigzip.c")
add_library(WIZ8_ZLIB_1_0_4 STATIC EXCLUDE_FROM_ALL ${WIZ8_ZLIB_UNITS})
target_compile_options(WIZ8_ZLIB_1_0_4 PRIVATE /nologo /O2 /MD)
target_compile_definitions(WIZ8_ZLIB_1_0_4 PRIVATE NDEBUG WIN32)
target_include_directories(WIZ8_ZLIB_1_0_4 PRIVATE "${ZLIB_SOURCE}")

include(sources.cmake)
set(WIZ8_SOURCE_FILES ${WIZ8_SOURCE_UNITS})
list(TRANSFORM WIZ8_SOURCE_FILES PREPEND "${PROJECT_SOURCE_DIR}/")

add_executable(WIZ8 WIN32 ${WIZ8_SOURCE_FILES})
target_compile_options(WIZ8 PRIVATE /Ob2)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Link the renderer built by this project. Only the fixed-arity assertion
    # declaration needs a separately named import of its variadic DLL export.
    set(sr_import_def "${CMAKE_CURRENT_BINARY_DIR}/sr-assert.def")
    file(WRITE "${sr_import_def}"
        "LIBRARY sr\nEXPORTS\n?srAssertFail@@YAXPBD0J0@Z EXPORTAS ?srAssertFail@@YAXPBD0J0ZZ\n")
    wiz8_add_import_library(WIZ8_SR "${sr_import_def}")
    target_link_libraries(WIZ8 PRIVATE SURRENDER)
else()
    set(SR_ASSERT_IMPORT_LIBRARY "${CMAKE_BINARY_DIR}/sr-assert-import.lib")
    if(NOT EXISTS "${SR_ASSERT_IMPORT_LIBRARY}" OR
       "${PROJECT_SOURCE_DIR}/cmake/SurRenderImport.py" IS_NEWER_THAN "${SR_ASSERT_IMPORT_LIBRARY}")
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/cmake/SurRenderImport.py"
                "${SR_ASSERT_IMPORT_LIBRARY}"
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()
    wiz8_add_import_library(WIZ8_SR imports/sr.def IMPORT_OBJECTS "${SR_ASSERT_IMPORT_LIBRARY}")
endif()
wiz8_add_import_library(WIZ8_MSS32 imports/mss32.def PRESERVE_C_DECORATION)
wiz8_add_import_library(WIZ8_BINKW32 imports/binkw32.def PRESERVE_C_DECORATION)
add_dependencies(WIZ8
    ${WIZ8_SR_IMPORT_TARGET} ${WIZ8_MSS32_IMPORT_TARGET} ${WIZ8_BINKW32_IMPORT_TARGET})
target_link_libraries(WIZ8 PRIVATE
    wiz8_compile_settings WIZ8_SGP WIZ8_ZLIB_1_0_4
    ddraw.lib gdi32.lib user32.lib
    "${WIZ8_SR_IMPORT_LIBRARY}" "${WIZ8_MSS32_IMPORT_LIBRARY}" "${WIZ8_BINKW32_IMPORT_LIBRARY}")
target_link_options(WIZ8 PRIVATE
    /DEBUG "$<$<CXX_COMPILER_ID:MSVC>:/DEBUGTYPE:CV>" "$<$<CXX_COMPILER_ID:MSVC>:/PDBTYPE:SEPT>" /INCREMENTAL:NO /OPT:REF /OPT:NOICF /FIXED
    /BASE:0x400000 /FILEALIGN:0x1000
    /OSVERSION:4.0 /SUBSYSTEM:WINDOWS,4.0
    /STACK:0x100000,0x1000 /HEAP:0x100000,0x1000)
set_target_properties(WIZ8 PROPERTIES OUTPUT_NAME Wiz8)
