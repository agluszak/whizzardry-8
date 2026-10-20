set(NATIVE_RENDER_SOURCES
    compositor.cpp
    camera.cpp
    clip_plane.cpp
    ../platform/assertion.cpp
    effects/fog.cpp
    pipeline/draw_inputs.cpp
    renderer.cpp
    vsg_material.cpp
    resources/render_mesh.cpp
    resources/image_io.cpp
    resources/image_tga.cpp
    effects/light.cpp
    resources/material.cpp
    resources/mesh.cpp
    resources/mesh_frames.cpp
    resources/mesh_instance.cpp
    world_scene.cpp
    resources/texture.cpp
)

list(TRANSFORM NATIVE_RENDER_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
target_sources(wiz8_engine PRIVATE ${NATIVE_RENDER_SOURCES})
target_link_libraries(wiz8_engine PUBLIC vsg::vsg
    PRIVATE wiz8_native_settings wiz8_platform SDL3::SDL3 SDL3_image::SDL3_image)
if(UNIX AND NOT APPLE)
    target_link_options(wiz8_engine PRIVATE -Wl,--no-undefined)
endif()

if(WIZ8_UNITY_BUILD OR WIZ8_RENDER_UNITY_BUILD)
    set_target_properties(wiz8_engine PROPERTIES
        UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE ${WIZ8_UNITY_BATCH_SIZE})
endif()

# All targets use SPIR-V. Keep shader compilation in the ordinary build and
# provide the generated headers independently for static-analysis configurations.
find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslangValidator.exe REQUIRED)
set(VSG_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(VSG_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/vsg_shaders")
set(VSG_SHADER_HEADERS)
foreach(shader world overlay)
    foreach(stage vert frag)
        set(header "${VSG_SHADER_DIR}/${shader}_${stage}.spv.h")
        add_custom_command(
            OUTPUT "${header}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${VSG_SHADER_DIR}"
            COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.0
                "-I${VSG_SHADER_SOURCE_DIR}" --vn ${shader}_${stage}
                -o "${header}" "${VSG_SHADER_SOURCE_DIR}/${shader}.${stage}"
            DEPENDS "${VSG_SHADER_SOURCE_DIR}/${shader}.${stage}"
                "${VSG_SHADER_SOURCE_DIR}/world_blocks.glsl"
                "${VSG_SHADER_SOURCE_DIR}/wizardry_vertex.glsl"
                "${VSG_SHADER_SOURCE_DIR}/wizardry_fragment.glsl"
            VERBATIM)
        list(APPEND VSG_SHADER_HEADERS "${header}")
    endforeach()
endforeach()
add_custom_target(wiz8_shader_headers DEPENDS ${VSG_SHADER_HEADERS})
add_dependencies(wiz8_engine wiz8_shader_headers)
target_sources(wiz8_engine PRIVATE ${VSG_SHADER_HEADERS})
target_include_directories(wiz8_engine PRIVATE "${VSG_SHADER_DIR}")
