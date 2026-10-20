# Rendering-library evaluation prototypes (WIZ8_RENDER_POC). Not part of the
# game: each prototype renders the same scene, built with the game's render
# types, through one library, and checks its vertex stage against the native
# shadeVertices. See README.md.

add_library(wiz8_render_poc STATIC
    common/demo_scene.cpp
    common/lighting_check.cpp
    common/poc_app.cpp
    common/resident_mesh.cpp
    common/shading_block.cpp
    common/transparent_order.cpp
)
target_include_directories(wiz8_render_poc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/common")
target_link_libraries(wiz8_render_poc PUBLIC
    wiz8_engine wiz8_native_settings SDL3::SDL3 SDL3_image::SDL3_image)
# The shaders are embedded with #embed; rebuild when they change.
set_property(SOURCE common/shading_block.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/common/shaders/wizardry_vertex.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/shaders/wizardry_fragment.glsl")

add_subdirectory(ogre)
