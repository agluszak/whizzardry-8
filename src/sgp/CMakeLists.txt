# Native SGP: recovered shell, input, CPU surfaces and portable core.
# Audio remains at the native media boundary.
add_library(WIZ8_SGP STATIC
    Container.cpp
    DEBUG.cpp
    MemMan.cpp
    FileMan.cpp
    vobject.cpp
    timer.cpp
    input.cpp
    native/input_events.cpp
    soundman.cpp
    native/audio.cpp
    native/surfaces.cpp
    native/video.cpp
    vsurface.cpp
    sgp.cpp
    native/application.cpp
    Font.cpp
    mousesystem.cpp
    "Button System.cpp"
    "Cursor Control.cpp"
    "Button Sound Control.cpp"
    RegInst.cpp
    LibraryDataBase.cpp
    WizLibs.cpp
    line.cpp
    himage.cpp
    STCI.cpp
    vobject_blitters.cpp
    shading.cpp
    Compression.cpp
    Random.cpp
    English.cpp
)
set_source_files_properties(DEBUG.cpp PROPERTIES COMPILE_DEFINITIONS _NO_DEBUG_TXT)
target_include_directories(WIZ8_SGP PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(WIZ8_SGP PUBLIC wiz8_native_settings ZLIB::ZLIB
    SDL3_image::SDL3_image SDL3_mixer::SDL3_mixer)

# Allow focused integration executables to discard unused game functions.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(WIZ8_SGP PRIVATE /Gy /Gw)
else()
    target_compile_options(WIZ8_SGP PRIVATE -ffunction-sections -fdata-sections)
endif()

target_link_libraries(WIZ8_SGP PRIVATE miniaudio)
