# OGRE prototype (WIZ8_RENDER_POC_OGRE): SDL3 owns the window and the OpenGL
# 3.3 core context; OGRE's GL3Plus render system adopts the current context.
option(WIZ8_RENDER_POC_OGRE "Build the OGRE prototype" ON)
if(NOT WIZ8_RENDER_POC_OGRE)
    return()
endif()

# OgreMain and the GL3Plus render system only: no components (Bites, RTSS,
# Overlay...), plugins, codecs, samples, tools or downloaded dependencies.
set(OGRE_STATIC ON CACHE BOOL "" FORCE)
set(OGRE_BUILD_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_ENABLE_ZIP OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(OGRE_INSTALL_SAMPLES OFF CACHE BOOL "" FORCE)
foreach(rendersystem D3D9 D3D11 GL GLES2 METAL VULKAN TINY)
    set(OGRE_BUILD_RENDERSYSTEM_${rendersystem} OFF CACHE BOOL "" FORCE)
endforeach()
set(OGRE_BUILD_RENDERSYSTEM_GL3PLUS ON CACHE BOOL "" FORCE)
foreach(plugin ASSIMP BSP OCTREE PFX DOT_SCENE PCZ CG GLSLANG FREEIMAGE EXRCODEC STBI RSIMAGE)
    set(OGRE_BUILD_PLUGIN_${plugin} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(component PAGING MESHLODGENERATOR TERRAIN VOLUME PROPERTY OVERLAY OVERLAY_IMGUI BITES
                  BULLET PYTHON JAVA CSHARP RTSHADERSYSTEM)
    set(OGRE_BUILD_COMPONENT_${component} OFF CACHE BOOL "" FORCE)
endforeach()
set(OGRE_BUILD_RTSHADERSYSTEM_SHADERS OFF CACHE BOOL "" FORCE)
# GLX, matching the context SDL3 creates on X11.
set(OGRE_GLSUPPORT_USE_EGL OFF CACHE BOOL "" FORCE)
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")
set(CMAKE_CXX_STANDARD 17)
FetchContent_Declare(ogre
    GIT_REPOSITORY https://github.com/OGRECave/ogre.git
    GIT_TAG f906880885f6014ca9b5bd8aaa09b399394d5676 # v14.6.1
    EXCLUDE_FROM_ALL SYSTEM)
FetchContent_MakeAvailable(ogre)

add_executable(wiz8_render_poc_ogre main.cpp)
target_include_directories(wiz8_render_poc_ogre SYSTEM PRIVATE
    "${ogre_SOURCE_DIR}/RenderSystems/GL3Plus/include")
target_link_libraries(wiz8_render_poc_ogre PRIVATE wiz8_render_poc OgreMain RenderSystem_GL3Plus)
set_target_properties(wiz8_render_poc_ogre PROPERTIES CXX_STANDARD 26)
