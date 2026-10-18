set(SURRENDER_SOURCES
    camera.cpp
    clipplane.cpp
    debug.cpp
    fog.cpp
    vertex_pipe.cpp
    gerd.cpp
    gerd_filters.cpp
    illuminator.cpp
    image_io.cpp
    image_tga.cpp
    light.cpp
    material.cpp
    mesh_model.cpp
    triangle_culler.cpp
    model_instance.cpp
    node.cpp
    scene.cpp
    renderer.cpp
    texture.cpp
)

add_library(SURRENDER STATIC ${SURRENDER_SOURCES})
target_link_libraries(SURRENDER PRIVATE wiz8_native_settings SDL3::SDL3 SDL3_image::SDL3_image)
if(UNIX AND NOT APPLE)
    target_link_options(SURRENDER PRIVATE -Wl,--no-undefined)
endif()
set_target_properties(SURRENDER PROPERTIES OUTPUT_NAME sr)

if(WIZ8_PRECOMPILE_HEADERS)
    target_precompile_headers(SURRENDER PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${PROJECT_SOURCE_DIR}/src/surrender/pch.h>")
endif()
if(WIZ8_UNITY_BUILD OR WIZ8_SURRENDER_UNITY_BUILD)
    set_target_properties(SURRENDER PROPERTIES
        UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE ${WIZ8_UNITY_BATCH_SIZE})
endif()

# SPIR-V is built from GLSL; offline SDL_shadercross outputs cover Metal/D3D12.
find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslangValidator.exe REQUIRED)
set(SDL_GPU_DEVICE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/devices/sdl_gpu")
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
# Lint-only configurations need generated shader headers without compiling SurRender.
add_custom_target(surrender_shader_headers DEPENDS ${SDL_GPU_SHADER_HEADERS})
target_sources(SURRENDER PRIVATE devices/sdl_gpu/sdl_gpu_device.cpp ${SDL_GPU_SHADER_HEADERS})
target_include_directories(SURRENDER PRIVATE "${SDL_GPU_SHADER_DIR}")
