set(SURRENDER_SOURCES
    bounder.cpp
    camera.cpp
    clipplane.cpp
    config.cpp
    core.cpp
    debug.cpp
    debug_dd.cpp
    debug_vp.cpp
    dynamic_library.cpp
    environment_mapper.cpp
    exponent_table.cpp
    extension.cpp
    fog.cpp
    vertex_pipe.cpp
    file_stream.cpp
    filter.cpp
    color_surface.cpp
    generic_vp.cpp
    gerd.cpp
    gerd_filters.cpp
    global_recycler.cpp
    heap.cpp
    huffman.cpp
    illuminator.cpp
    io_managers.cpp
    light.cpp
    material.cpp
    memory_pool.cpp
    mesh_model.cpp
    triangle_culler.cpp
    model.cpp
    model_instance.cpp
    modeler.cpp
    mutex.cpp
    node.cpp
    scene.cpp
    stream.cpp
    string_table.cpp
    system.cpp
    palette.cpp
    pixel_convert.cpp
    renderer.cpp
    texture.cpp
    texture_file.cpp
    texture_map.cpp
    scheduler.cpp
    statistics.cpp
    thread.cpp
    timer.cpp
    variable_timer.cpp
    triangulator.cpp
    type_registry.cpp
    video_manager.cpp
    window.cpp
    window_out.cpp
    vector_processor.cpp
)

add_library(SURRENDER STATIC ${SURRENDER_SOURCES})
target_compile_definitions(SURRENDER PRIVATE
    SURRENDER_BUILD
    SR_LIBRARY_EXTENSION="${CMAKE_SHARED_LIBRARY_SUFFIX}"
)
target_link_libraries(SURRENDER PRIVATE wiz8_native_settings SDL3::SDL3 SDL3_image::SDL3_image)
if(UNIX AND NOT APPLE)
    target_link_options(SURRENDER PRIVATE -Wl,--no-undefined)
endif()
set_target_properties(SURRENDER PROPERTIES OUTPUT_NAME sr)

# SDL3 GPU device: GLSL compiled to SPIR-V headers at build time.
find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslangValidator.exe
    HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/glslang"
          "${_VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/glslang"
          "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_HOST_TRIPLET}/tools/glslang"
    REQUIRED)
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
endforeach()
target_sources(SURRENDER PRIVATE devices/sdl_gpu/sdl_gpu_device.cpp ${SDL_GPU_SHADER_HEADERS})
target_include_directories(SURRENDER PRIVATE "${SDL_GPU_SHADER_DIR}")
