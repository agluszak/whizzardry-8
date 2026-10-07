set(IJG_JPEG_SOURCE "" CACHE PATH "Pinned IJG release 6 source tree")

# Product link order recovered from the original address map. In
# particular, the two memory-manager objects precede quantization and
# jutils; the discovery glob remains appropriate only for FID seeds.
set(IJG_JPEG_UNITS
    jcapimin.c
    jcapistd.c
    jccoefct.c
    jccolor.c
    jcdctmgr.c
    jchuff.c
    jcinit.c
    jcmainct.c
    jcmarker.c
    jcmaster.c
    jcomapi.c
    jcparam.c
    jcphuff.c
    jcprepct.c
    jcsample.c
    jctrans.c
    jdapimin.c
    jdapistd.c
    jdatadst.c
    jdatasrc.c
    jdcoefct.c
    jdcolor.c
    jddctmgr.c
    jdhuff.c
    jdinput.c
    jdmainct.c
    jdmarker.c
    jdmaster.c
    jdmerge.c
    jdphuff.c
    jdpostct.c
    jdsample.c
    jdtrans.c
    jerror.c
    jfdctflt.c
    jfdctfst.c
    jfdctint.c
    jidctflt.c
    jidctfst.c
    jidctint.c
    jidctred.c
    jmemmgr.c
    jmemnobs.c
    jquant1.c
    jquant2.c
    jutils.c
)
set(IJG_JPEG_SOURCES ${IJG_JPEG_UNITS})
list(TRANSFORM IJG_JPEG_SOURCES PREPEND "${IJG_JPEG_SOURCE}/")

set(IJG_STDIO_SOURCES
    "${IJG_JPEG_SOURCE}/jdatadst.c"
    "${IJG_JPEG_SOURCE}/jdatasrc.c"
)
set_source_files_properties(${IJG_STDIO_SOURCES} PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ijg_stdio_override.h"
    COMPILE_OPTIONS "/FI${CMAKE_CURRENT_SOURCE_DIR}/ijg_stdio_override.h"
)
# The simple lifetime functions (constructors, destructors, accessors) have
# no C++ EH prologue in the original; getSurfaceDesc/importSurface/
# exportSurface do have one (a synthesized SEH frame installed via
# fs:[0]). They are split into surface_transfer.cpp, compiled at the
# project default /GX, so plugin.cpp can keep /GX- for the rest.
set_source_files_properties(plugin.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/GX->")

# Recovered adapter code contributes the first and last object groups; the
# pristine IJG translation units sit between them in original link order.
add_library(wiz8_jpeg_codec_objects OBJECT codec_adapter.cpp)
add_library(wiz8_jpeg_plugin_objects OBJECT
    stream_bridge.cpp
    plugin.cpp
    surface_transfer.cpp
)
foreach(target IN ITEMS wiz8_jpeg_codec_objects wiz8_jpeg_plugin_objects)
    target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/cmake/jpeg"
        "${IJG_JPEG_SOURCE}"
    )
    target_link_libraries(${target} PRIVATE wiz8_compile_settings)
endforeach()

wiz8_add_import_library(
    SREXT_JPEG_SR
    sr-jpeg-imports.def
)

wiz8_compile_resource(SREXT_JPEG_RESOURCE_OBJECT srEXT_JPEGImporter.rc)

add_library(SREXT_JPEGIMPORTER SHARED
    $<TARGET_OBJECTS:wiz8_jpeg_codec_objects>
    ${IJG_JPEG_SOURCES}
    $<TARGET_OBJECTS:wiz8_jpeg_plugin_objects>
    srEXT_JPEGImporter.def
    "${SREXT_JPEG_RESOURCE_OBJECT}"
)
add_dependencies(
    SREXT_JPEGIMPORTER
    ${SREXT_JPEG_SR_IMPORT_TARGET}
)
target_include_directories(SREXT_JPEGIMPORTER PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/cmake/jpeg"
    "${IJG_JPEG_SOURCE}"
)
target_link_libraries(
    SREXT_JPEGIMPORTER
    PRIVATE
        wiz8_compile_settings
        "${SREXT_JPEG_SR_IMPORT_LIBRARY}"
)
target_link_options(SREXT_JPEGIMPORTER PRIVATE
    /DEBUG /INCREMENTAL:NO /BASE:0x10000000
    /DEBUGTYPE:CV
    /OPT:NOREF
    /OPT:NOICF
)
set_target_properties(SREXT_JPEGIMPORTER PROPERTIES OUTPUT_NAME srEXT_JPEGImporter)
