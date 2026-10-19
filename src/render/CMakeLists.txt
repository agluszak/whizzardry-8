set(NATIVE_RENDER_SOURCES
    compositor.cpp
    camera.cpp
    clip_plane.cpp
    ../platform/assertion.cpp
    effects/fog.cpp
    pipeline/vertex_shading.cpp
    pipeline/gpu_renderer.cpp
    pipeline/texture_filters.cpp
    resources/image_io.cpp
    resources/image_tga.cpp
    effects/light.cpp
    resources/material.cpp
    resources/mesh.cpp
    resources/mesh_frames.cpp
    pipeline/mesh_culling.cpp
    resources/mesh_instance.cpp
    world_scene.cpp
    pipeline/mesh_batches.cpp
    pipeline/mesh_submission.cpp
    resources/texture.cpp
)

list(TRANSFORM NATIVE_RENDER_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
target_sources(wiz8_engine PRIVATE ${NATIVE_RENDER_SOURCES})
target_link_libraries(wiz8_engine PRIVATE wiz8_native_settings wiz8_platform SDL3::SDL3 SDL3_image::SDL3_image)
if(UNIX AND NOT APPLE)
    target_link_options(wiz8_engine PRIVATE -Wl,--no-undefined)
endif()

if(WIZ8_UNITY_BUILD OR WIZ8_RENDER_UNITY_BUILD)
    set_target_properties(wiz8_engine PROPERTIES
        UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE ${WIZ8_UNITY_BATCH_SIZE})
endif()

# SPIR-V is built from GLSL; offline SDL_shadercross outputs cover Metal/D3D12.
find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslangValidator.exe REQUIRED)
set(SDL_GPU_DEVICE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SDL_GPU_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/sdl_gpu_shaders")
set(SDL_GPU_SHADER_HEADERS)
foreach(shader vertex fragment)
    if(shader STREQUAL "vertex")
        set(stage vert)
    else()
        set(stage frag)
    endif()
    set(header "${SDL_GPU_SHADER_DIR}/sr_${shader}.spv.h")
    add_custom_command(
        OUTPUT "${header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SDL_GPU_SHADER_DIR}"
        COMMAND "${GLSLANG_VALIDATOR}" -V -S ${stage} --vn sr_${shader}_spv
            -o "${header}" "${SDL_GPU_DEVICE_DIR}/sr_${shader}.glsl"
        DEPENDS "${SDL_GPU_DEVICE_DIR}/sr_${shader}.glsl"
        VERBATIM
    )
    list(APPEND SDL_GPU_SHADER_HEADERS "${header}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${SDL_GPU_DEVICE_DIR}/sr_${shader}.glsl"
        "${SDL_GPU_DEVICE_DIR}/compiled/sr_${shader}.sha256")
    file(SHA256 "${SDL_GPU_DEVICE_DIR}/sr_${shader}.glsl" source_digest)
    file(READ "${SDL_GPU_DEVICE_DIR}/compiled/sr_${shader}.sha256" embedded_digest)
    string(STRIP "${embedded_digest}" embedded_digest)
    if(NOT source_digest STREQUAL embedded_digest)
        message(FATAL_ERROR "Stale ${shader} shaders: regenerate with tools/shadercross/generate.py")
    endif()
    foreach(format msl dxil)
        set(input "${SDL_GPU_DEVICE_DIR}/compiled/sr_${shader}.${format}")
        set(header "${SDL_GPU_SHADER_DIR}/sr_${shader}.${format}.h")
        add_custom_command(OUTPUT "${header}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${SDL_GPU_SHADER_DIR}"
            COMMAND "${CMAKE_COMMAND}" "-DINPUT=${input}" "-DOUTPUT=${header}"
                "-DSYMBOL=sr_${shader}_${format}"
                -P "${PROJECT_SOURCE_DIR}/cmake/EmbedShader.cmake"
            DEPENDS "${input}" "${PROJECT_SOURCE_DIR}/cmake/EmbedShader.cmake"
            VERBATIM)
        list(APPEND SDL_GPU_SHADER_HEADERS "${header}")
    endforeach()
endforeach()
# Lint-only configurations need generated shader headers without compiling the engine.
add_custom_target(wiz8_shader_headers DEPENDS ${SDL_GPU_SHADER_HEADERS})
add_dependencies(wiz8_engine wiz8_shader_headers)
target_sources(wiz8_engine PRIVATE pipeline/gpu_commands.cpp ${SDL_GPU_SHADER_HEADERS})
target_include_directories(wiz8_engine PRIVATE "${SDL_GPU_SHADER_DIR}")
