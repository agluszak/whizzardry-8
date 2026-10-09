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
    native/window_procedure.cpp
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
    PCX.cpp
    impTGA.cpp
    vobject_blitters.cpp
    shading.cpp
    Compression.cpp
    Random.cpp
    English.cpp
)
set_source_files_properties(DEBUG.cpp PROPERTIES COMPILE_DEFINITIONS _NO_DEBUG_TXT)
target_include_directories(WIZ8_SGP PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(WIZ8_SGP PUBLIC wiz8_native_settings ZLIB::ZLIB)

# Allow focused integration executables to link recovered functions before media ports.
target_compile_options(WIZ8_SGP PRIVATE -ffunction-sections -fdata-sections)

add_library(wiz8_miniaudio STATIC native/miniaudio.c)
target_include_directories(wiz8_miniaudio PUBLIC "${PROJECT_SOURCE_DIR}/third_party/miniaudio")
target_link_libraries(wiz8_miniaudio PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
target_link_libraries(WIZ8_SGP PRIVATE wiz8_miniaudio)
