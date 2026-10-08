# Reconstructed Wizardry SGP project. The released SFI project is the source
# ancestor; this list records the object contributions present in Wizardry.

if(WIZ8_NATIVE)
    include(native.cmake)
    return()
endif()

add_library(WIZ8_SGP STATIC
    sgp.cpp
    input.cpp
    vsurface.cpp
    Container.cpp
    DEBUG.cpp
    MemMan.cpp
    FileMan.cpp
    vobject.cpp
    timer.cpp
    Font.cpp
    soundman.cpp
    mousesystem.cpp
    "Button System.cpp"
    "Cursor Control.cpp"
    "Button Sound Control.cpp"
    RegInst.cpp
    "DirectDraw Calls.cpp"
    "DirectX Common.cpp"
    LibraryDataBase.cpp
    WizLibs.cpp
    line.cpp
    himage.cpp
    STCI.cpp
    PCX.cpp
    impTGA.cpp
    vobject_blitters.cpp
    shading.cpp
    Compression.cpp
    Random.cpp
    English.cpp
)

set_source_files_properties(DEBUG.cpp PROPERTIES COMPILE_DEFINITIONS _NO_DEBUG_TXT)
target_compile_definitions(WIZ8_SGP PRIVATE CINTERFACE COBJMACROS)
target_compile_options(WIZ8_SGP PRIVATE
    /nologo /Z7 /O2 /Ob2
    "/FI${PROJECT_SOURCE_DIR}/include/wiz8/compat/compiler.h"
)
target_compile_options(WIZ8_SGP PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/G5>" /MD)
target_include_directories(WIZ8_SGP PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# Headers of the zlib 1.0.4 release that WIZ8_ZLIB_1_0_4 compiles.
target_include_directories(WIZ8_SGP PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/zlib-1.0.4")
target_include_directories(WIZ8_SGP PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/wiz8"
    "${PROJECT_SOURCE_DIR}/include/wiz8/engine_code"
)
# DirectX Common.cpp owns the two DirectDraw IIDs; search the SDK archive after
# extracting the SGP objects for the remaining GUIDs.
target_link_libraries(WIZ8_SGP PUBLIC dxguid.lib)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(WIZ8_SGP PRIVATE /Zc:wchar_t-)
    target_compile_definitions(WIZ8_SGP PRIVATE WIZ8_CLANG_LINT _CRT_SECURE_NO_WARNINGS)
endif()
