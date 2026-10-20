# SDL 3.4 GPU 2D renderer prototype for the UI canvas (WIZ8_RENDER_POC_UI).
# Uses only SDL and the game's canvas code; no new dependency.
option(WIZ8_RENDER_POC_UI "Build the SDL GPU UI canvas prototype" ON)
if(NOT WIZ8_RENDER_POC_UI)
    return()
endif()

# SPIR-V only: the game's Metal and Direct3D outputs come from SDL_shadercross
# (tools/shadercross), which a real port would extend to these two shaders.
set(ui_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(ui_shader_headers)
foreach(shader sprite shade)
    set(header "${ui_shader_dir}/${shader}_frag.spv.h")
    add_custom_command(OUTPUT "${header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${ui_shader_dir}"
        COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.0 --vn ${shader}_frag
            -o "${header}" "${CMAKE_CURRENT_SOURCE_DIR}/${shader}.frag"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${shader}.frag" VERBATIM)
    list(APPEND ui_shader_headers "${header}")
endforeach()

add_executable(wiz8_render_poc_ui main.cpp gpu_canvas.cpp ${ui_shader_headers})
target_include_directories(wiz8_render_poc_ui PRIVATE "${ui_shader_dir}")
target_link_libraries(wiz8_render_poc_ui PRIVATE wiz8_render_poc)
