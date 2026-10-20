# Magnum prototype (WIZ8_RENDER_POC_MAGNUM): SDL3 owns the window and the
# OpenGL 3.3 core context; Magnum's GL wrappers and SceneGraph draw into it.
option(WIZ8_RENDER_POC_MAGNUM "Build the Magnum prototype" ON)
if(NOT WIZ8_RENDER_POC_MAGNUM)
    return()
endif()

# Only the GL and SceneGraph libraries: no applications (Magnum has no SDL3
# application; SDL3 creates the context), shaders, importers or tools.
set(CORRADE_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(CORRADE_WITH_TESTSUITE OFF CACHE BOOL "" FORCE)
set(CORRADE_WITH_INTERCONNECT OFF CACHE BOOL "" FORCE)
set(MAGNUM_BUILD_STATIC ON CACHE BOOL "" FORCE)
foreach(component AUDIO DEBUGTOOLS MATERIALTOOLS MESHTOOLS PRIMITIVES SCENETOOLS SHADERS
                  SHADERTOOLS TEXT TEXTURETOOLS TRADE)
    set(MAGNUM_WITH_${component} OFF CACHE BOOL "" FORCE)
endforeach()
set(MAGNUM_WITH_GL ON CACHE BOOL "" FORCE)
set(MAGNUM_WITH_SCENEGRAPH ON CACHE BOOL "" FORCE)
# The GL function loader for an externally created context is per platform:
# GlxContext here, WglContext on Windows and CglContext on macOS.
set(MAGNUM_WITH_GLXCONTEXT ON CACHE BOOL "" FORCE)
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")
set(CMAKE_CXX_STANDARD 17)
FetchContent_Declare(corrade
    GIT_REPOSITORY https://github.com/mosra/corrade.git
    GIT_TAG 4dc198963d0eb6965c5745ca1a734d6458588a92 # master, 2026-10-15
    EXCLUDE_FROM_ALL SYSTEM)
FetchContent_Declare(magnum
    GIT_REPOSITORY https://github.com/mosra/magnum.git
    GIT_TAG 369e9620ab22224ae66a376290b9b36e872209ce # master, 2026-10-19
    EXCLUDE_FROM_ALL SYSTEM)
FetchContent_MakeAvailable(corrade magnum)
list(APPEND CMAKE_MODULE_PATH "${corrade_SOURCE_DIR}/modules" "${magnum_SOURCE_DIR}/modules")
find_package(Magnum REQUIRED GL GlxContext SceneGraph)

add_executable(wiz8_render_poc_magnum main.cpp)
target_link_libraries(wiz8_render_poc_magnum PRIVATE
    wiz8_render_poc Magnum::Magnum Magnum::GL Magnum::GLContext Magnum::SceneGraph)
set_target_properties(wiz8_render_poc_magnum PROPERTIES CXX_STANDARD 26)
