# VulkanSceneGraph prototype (WIZ8_RENDER_POC_VSG): SDL3 owns the window and its
# Vulkan surface; VSG owns the device, swapchain, resources, the scene graph and
# recording.
option(WIZ8_RENDER_POC_VSG "Build the VulkanSceneGraph prototype" ON)
if(NOT WIZ8_RENDER_POC_VSG)
    return()
endif()

# Shaders are compiled at build time, as the game's are, and SDL3 creates the
# window, so VSG needs neither its runtime GLSL compiler nor native windowing.
set(VSG_SUPPORTS_ShaderCompiler OFF CACHE BOOL "" FORCE)
set(VSG_SUPPORTS_Windowing OFF CACHE BOOL "" FORCE)
FetchContent_Declare(vsg
    GIT_REPOSITORY https://github.com/vsg-dev/VulkanSceneGraph.git
    GIT_TAG 77839c704362d832c027cc878e5496a14972e83d # v1.1.16
    EXCLUDE_FROM_ALL SYSTEM)
# Build VSG with its own flags, not the game's warning set.
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")
set(CMAKE_CXX_STANDARD 17)
FetchContent_MakeAvailable(vsg)

set(shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
add_executable(wiz8_render_poc_glsl_layout glsl_layout.cpp)
target_include_directories(wiz8_render_poc_glsl_layout PRIVATE ../common)
target_link_libraries(wiz8_render_poc_glsl_layout PRIVATE wiz8_native_settings)
set_target_properties(wiz8_render_poc_glsl_layout PROPERTIES CXX_STANDARD 26)
add_custom_command(OUTPUT "${shader_dir}/wizardry_layout.glsl"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${shader_dir}"
    COMMAND wiz8_render_poc_glsl_layout "${shader_dir}/wizardry_layout.glsl"
    DEPENDS wiz8_render_poc_glsl_layout VERBATIM)
set(vsg_shader_headers)
foreach(stage vert frag)
    set(header "${shader_dir}/world_${stage}.spv.h")
    add_custom_command(OUTPUT "${header}"
        COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.0
            "-I${shader_dir}" "-I${CMAKE_CURRENT_SOURCE_DIR}"
            "-I${CMAKE_CURRENT_SOURCE_DIR}/../common/shaders"
            --vn world_${stage} -o "${header}" "${CMAKE_CURRENT_SOURCE_DIR}/world.${stage}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/world.${stage}"
            "${CMAKE_CURRENT_SOURCE_DIR}/world_blocks.glsl" "${shader_dir}/wizardry_layout.glsl"
            "${CMAKE_CURRENT_SOURCE_DIR}/../common/shaders/wizardry_vertex.glsl"
            "${CMAKE_CURRENT_SOURCE_DIR}/../common/shaders/wizardry_fragment.glsl"
        VERBATIM)
    list(APPEND vsg_shader_headers "${header}")
endforeach()

add_executable(wiz8_render_poc_vsg main.cpp world.cpp ${vsg_shader_headers})
target_include_directories(wiz8_render_poc_vsg PRIVATE "${shader_dir}")
target_link_libraries(wiz8_render_poc_vsg PRIVATE wiz8_render_poc vsg::vsg)
set_target_properties(wiz8_render_poc_vsg PROPERTIES CXX_STANDARD 26)
