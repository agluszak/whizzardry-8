# Native SGP. Units join this list as their Windows dependencies are replaced.
add_library(WIZ8_SGP STATIC
    FileMan.cpp
    LibraryDataBase.cpp
    WizLibs.cpp
    Compression.cpp
    DEBUG.cpp
    MemMan.cpp
    vobject_blitters.cpp
)
set_source_files_properties(DEBUG.cpp PROPERTIES COMPILE_DEFINITIONS _NO_DEBUG_TXT)
target_include_directories(WIZ8_SGP PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(WIZ8_SGP PUBLIC wiz8_native_settings ZLIB::ZLIB)
