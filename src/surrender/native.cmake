# SurRender as a native shared library. Windows-only translation units are
# replaced by native/ implementations; device drivers are compiled in rather
# than loaded from srDD_*.dll files.
set(SURRENDER_WINDOWS_SOURCES
    dynamic_library.cpp mutex.cpp system.cpp thread.cpp window.cpp window_out.cpp)
set(SURRENDER_NATIVE_SOURCES ${SURRENDER_SOURCES})
list(REMOVE_ITEM SURRENDER_NATIVE_SOURCES ${SURRENDER_WINDOWS_SOURCES})
list(TRANSFORM SURRENDER_WINDOWS_SOURCES PREPEND native/)

add_library(SURRENDER SHARED ${SURRENDER_NATIVE_SOURCES} ${SURRENDER_WINDOWS_SOURCES})
target_compile_definitions(SURRENDER PRIVATE SURRENDER_BUILD)
target_link_libraries(SURRENDER PRIVATE wiz8_native_settings SDL3::SDL3)
if(NOT APPLE)
    target_link_options(SURRENDER PRIVATE -Wl,--no-undefined)
endif()
set_target_properties(SURRENDER PROPERTIES OUTPUT_NAME sr)

# SDL3 GPU device: GLSL compiled to SPIR-V headers at build time.
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)
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
