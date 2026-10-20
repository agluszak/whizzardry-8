# OpenSceneGraph prototype (WIZ8_RENDER_POC_OSG): SDL3 owns the window and the
# OpenGL 3.3 core context; OSG renders into it through GraphicsWindowEmbedded.
option(WIZ8_RENDER_POC_OSG "Build the OpenSceneGraph prototype" ON)
if(NOT WIZ8_RENDER_POC_OSG)
    return()
endif()

find_package(OpenGL REQUIRED)
# A core profile without fixed function, no native windowing (SDL3 owns the
# window), a static build and none of the plugins or applications.
set(OPENGL_PROFILE GLCORE CACHE STRING "" FORCE)
# OSG's core-profile build still uses S3TC sRGB enums that only glext.h defines.
set(OPENGL_HEADER2 "#include <GL/glext.h>" CACHE STRING "" FORCE)
set(OSG_WINDOWING_SYSTEM None CACHE STRING "" FORCE)
set(DYNAMIC_OPENSCENEGRAPH OFF CACHE BOOL "" FORCE)
set(DYNAMIC_OPENTHREADS OFF CACHE BOOL "" FORCE)
set(BUILD_OSG_APPLICATIONS OFF CACHE BOOL "" FORCE)
set(BUILD_OSG_PLUGINS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(BUILD_OSG_DEPRECATED_SERIALIZERS OFF CACHE BOOL "" FORCE)
set(OSG_FIND_3RD_PARTY_DEPS OFF CACHE BOOL "" FORCE)
# OSG's build scripts predate CMake 3.5 and its sources predate C++20.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_CXX_STANDARD 17)
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")
FetchContent_Declare(openscenegraph
    GIT_REPOSITORY https://github.com/openscenegraph/OpenSceneGraph.git
    GIT_TAG 2e4ae2ea94595995c1fc56860051410b0c0be605 # master, 2022-12-01 (3.6.5 is from 2020)
    EXCLUDE_FROM_ALL SYSTEM)
FetchContent_MakeAvailable(openscenegraph)

add_executable(wiz8_render_poc_osg main.cpp)
target_include_directories(wiz8_render_poc_osg SYSTEM PRIVATE
    "${openscenegraph_SOURCE_DIR}/include" "${openscenegraph_BINARY_DIR}/include")
target_compile_definitions(wiz8_render_poc_osg PRIVATE OSG_LIBRARY_STATIC)
target_link_libraries(wiz8_render_poc_osg PRIVATE
    wiz8_render_poc osgViewer osgGA osgText osgDB osgUtil osg OpenThreads OpenGL::GL)
set_target_properties(wiz8_render_poc_osg PROPERTIES CXX_STANDARD 26)
