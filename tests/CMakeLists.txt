link_libraries(Catch2::Catch2WithMain wiz8_asset_preparation)

# Reuse the runtime clock hook for deterministic timer checks without game assets.
add_executable(native_game_timer_test native/game_timer_test.cpp
    "${PROJECT_SOURCE_DIR}/src/compat/native_helpers.cpp")
target_compile_definitions(native_game_timer_test PRIVATE WIZ8_RUNTIME_TESTS=1)
target_link_libraries(native_game_timer_test PRIVATE WIZ8_GAME_CORE)
catch_discover_tests(native_game_timer_test TEST_PREFIX "native_game_timer/")

add_executable(native_game_geometry_test native/game_geometry_test.cpp)
target_link_libraries(native_game_geometry_test PRIVATE WIZ8_GAME_CORE)
catch_discover_tests(native_game_geometry_test TEST_PREFIX "native_game_geometry/")

add_executable(native_hash_consumers_test native/hash_consumers_test.cpp)
target_link_libraries(native_hash_consumers_test PRIVATE SURRENDER wiz8_native_settings)
add_test(NAME native_hash_consumers COMMAND native_hash_consumers_test)

add_executable(native_bit_array_io_test native/bit_array_io_test.cpp)
target_link_libraries(native_bit_array_io_test PRIVATE WIZ8_GAME_CORE)
add_test(NAME native_bit_array_io COMMAND native_bit_array_io_test)

add_executable(native_images_test native/images_test.cpp)
target_link_libraries(native_images_test PRIVATE WIZ8_GAME_CORE)
catch_discover_tests(native_images_test TEST_PREFIX "native_images/")

add_executable(native_files_test "${PROJECT_SOURCE_DIR}/tests/native/files_test.cpp")
target_link_libraries(native_files_test PRIVATE WIZ8_GAME_CORE SURRENDER)
catch_discover_tests(native_files_test TEST_PREFIX "native_files/")

add_executable(native_strings_test native/strings_test.cpp)
target_link_libraries(native_strings_test PRIVATE SURRENDER wiz8_native_settings)
add_test(NAME native_strings COMMAND native_strings_test)

add_executable(native_events_test "${PROJECT_SOURCE_DIR}/tests/native/events_test.cpp")
target_link_libraries(native_events_test PRIVATE WIZ8_GAME_CORE SDL3::SDL3)
add_test(NAME native_events COMMAND native_events_test)
set_tests_properties(native_events PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy" TIMEOUT 10)

add_executable(native_imports_test "${PROJECT_SOURCE_DIR}/tests/native/imports_test.cpp")
target_link_libraries(native_imports_test PRIVATE SURRENDER wiz8_native_settings)
add_test(NAME native_imports COMMAND native_imports_test)

foreach(fixture mesh_ownership renderer_ownership)
    add_executable(native_${fixture}_test native/${fixture}_test.cpp)
    target_link_libraries(native_${fixture}_test PRIVATE SURRENDER wiz8_native_settings)
    add_test(NAME native_${fixture} COMMAND native_${fixture}_test)
endforeach()
set_tests_properties(native_renderer_ownership PROPERTIES TIMEOUT 15)

add_executable(native_mesh_frames_test native/mesh_frames_test.cpp)
target_link_libraries(native_mesh_frames_test PRIVATE WIZ8_GAME_CORE)
if(APPLE)
    target_link_options(native_mesh_frames_test PRIVATE -Wl,-dead_strip)
elseif(UNIX)
    target_link_options(native_mesh_frames_test PRIVATE -Wl,--gc-sections -Wl,-z,start-stop-gc)
endif()
catch_discover_tests(native_mesh_frames_test TEST_PREFIX "native_mesh_frames/")

add_executable(native_image_io_test native/image_io_test.cpp)
target_link_libraries(native_image_io_test PRIVATE SURRENDER wiz8_native_settings)
add_test(NAME native_image_io COMMAND native_image_io_test)

add_executable(native_renderer_platform_test native/renderer_platform_test.cpp)
target_link_libraries(native_renderer_platform_test PRIVATE SURRENDER wiz8_native_settings)
add_test(NAME native_renderer_platform COMMAND native_renderer_platform_test)
set_tests_properties(native_renderer_platform PROPERTIES TIMEOUT 10)

add_executable(native_runtime_lookup_test native/runtime_lookup_test.cpp)
target_link_libraries(native_runtime_lookup_test PRIVATE SURRENDER wiz8_native_settings)
add_test(NAME native_runtime_lookup COMMAND native_runtime_lookup_test)

add_executable(native_surrender_test native/surrender_test.cpp)
target_link_libraries(native_surrender_test PRIVATE SURRENDER wiz8_native_settings)
add_test(NAME native_surrender COMMAND native_surrender_test)

add_executable(native_unicode_test "${PROJECT_SOURCE_DIR}/tests/native/unicode_test.cpp")
target_link_libraries(native_unicode_test PRIVATE wiz8_native_settings)
catch_discover_tests(native_unicode_test
    TEST_PREFIX "native_unicode/")

add_executable(native_world_lookup_test "${PROJECT_SOURCE_DIR}/tests/native/world_lookup_test.cpp")
target_link_libraries(native_world_lookup_test PRIVATE WIZ8_GAME_CORE)
if(APPLE)
    target_link_options(native_world_lookup_test PRIVATE -Wl,-dead_strip)
elseif(UNIX)
    target_link_options(native_world_lookup_test PRIVATE -Wl,--gc-sections -Wl,-z,start-stop-gc)
endif()
add_test(NAME native_world_lookup COMMAND native_world_lookup_test)

add_executable(native_save_records_test "${PROJECT_SOURCE_DIR}/tests/native/save_records_test.cpp")
target_compile_definitions(native_save_records_test PRIVATE
    WIZ8_CHARACTER_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/fixtures/vi.CHR")
target_link_libraries(native_save_records_test PRIVATE WIZ8_GAME_CORE SDL3::SDL3)
if(APPLE)
    target_link_options(native_save_records_test PRIVATE -Wl,-dead_strip)
elseif(UNIX)
    target_link_options(native_save_records_test PRIVATE -Wl,--gc-sections -Wl,-z,start-stop-gc)
endif()
catch_discover_tests(native_save_records_test
    TEST_PREFIX "native_save_records/"
    PROPERTIES TIMEOUT 120)

add_executable(native_loader_records_test native/loader_records_test.cpp)
target_link_libraries(native_loader_records_test PRIVATE WIZ8_GAME_CORE)
if(APPLE)
    target_link_options(native_loader_records_test PRIVATE -Wl,-dead_strip)
elseif(UNIX)
    target_link_options(native_loader_records_test PRIVATE -Wl,--gc-sections -Wl,-z,start-stop-gc)
endif()
catch_discover_tests(native_loader_records_test TEST_PREFIX "native_loader_records/")

add_executable(native_jpeg_transfer_test "${PROJECT_SOURCE_DIR}/tests/native/jpeg_transfer_test.cpp")
target_include_directories(native_jpeg_transfer_test PRIVATE "${PROJECT_SOURCE_DIR}/src/surrender")
target_compile_definitions(native_jpeg_transfer_test PRIVATE WIZ8_IMAGE_IMPORT_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/native/image_import")
target_link_libraries(native_jpeg_transfer_test PRIVATE SURRENDER wiz8_native_settings SDL3_image::SDL3_image)
catch_discover_tests(native_jpeg_transfer_test TEST_PREFIX "native_image_transfer/")

add_executable(native_surface_test native/surface_test.cpp)
target_link_libraries(native_surface_test PRIVATE WIZ8_GAME_CORE)
add_test(NAME native_surfaces COMMAND native_surface_test)

add_executable(native_owner_test native/owner_test.cpp)
target_link_libraries(native_owner_test PRIVATE WIZ8_GAME_CORE SDL3::SDL3)
if(APPLE)
    target_link_options(native_owner_test PRIVATE -Wl,-dead_strip)
elseif(UNIX)
    target_link_options(native_owner_test PRIVATE -Wl,--gc-sections -Wl,-z,start-stop-gc)
endif()
catch_discover_tests(native_owner_test
    TEST_PREFIX "native_owners/"
    TEST_SPEC "~[.death]"
    PROPERTIES TIMEOUT 10)
foreach(mode plain button-self-destroy)
    add_test(NAME native_assertion_${mode}
        COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:native_owner_test>" "-DMODE=${mode}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/assertion_failure.cmake")
endforeach()


add_executable(native_vector_math_test native/vector_math_test.cpp)
target_link_libraries(native_vector_math_test PRIVATE SURRENDER wiz8_native_settings)
add_test(NAME native_vector_math COMMAND native_vector_math_test)

add_executable(native_blitter_test "${PROJECT_SOURCE_DIR}/tests/native/blitter_test.cpp")
target_compile_definitions(native_blitter_test PRIVATE
    WIZ8_BLITTER_EXPECTED="${PROJECT_SOURCE_DIR}/tests/native/blitters_expected.txt")
target_link_libraries(native_blitter_test PRIVATE WIZ8_GAME_CORE)
add_test(NAME native_blitters COMMAND native_blitter_test)

# Check first-party objects, not SDL's statically linked host-width code.
if(UNIX)
    add_test(NAME native_no_glibc_wide_strings
        COMMAND "${CMAKE_COMMAND}" "-DNM=${CMAKE_NM}"
            "-DBINARIES=$<TARGET_FILE:wiz8_compat>;$<TARGET_FILE:wiz8_filesystem>;$<TARGET_FILE:SURRENDER>;$<TARGET_FILE:WIZ8_GAME_CORE>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/no_host_wide_strings.cmake")
endif()

add_executable(native_game_graphics "${PROJECT_SOURCE_DIR}/tests/native/game_graphics.cpp")
target_compile_definitions(native_game_graphics PRIVATE
    WIZ8_MOVIE_FIXTURE="${PROJECT_SOURCE_DIR}/tests/native/movie.mkv")
target_link_libraries(native_game_graphics PRIVATE WIZ8_GAME_CORE SDL3::SDL3)
if(APPLE)
    target_link_options(native_game_graphics PRIVATE -Wl,-dead_strip)
elseif(UNIX)
    # Discard unused ASan global-registration sections alongside unused game
    # functions in this focused graphics harness.
    target_link_options(native_game_graphics PRIVATE -Wl,--gc-sections -Wl,-z,start-stop-gc)
endif()
# Uses installed retail assets, supplied through WIZ8_ASSET_ROOT; run explicitly.

add_executable(native_audio_test "${PROJECT_SOURCE_DIR}/tests/native/audio_test.cpp")
target_link_libraries(native_audio_test PRIVATE WIZ8_GAME_CORE)
target_compile_definitions(native_audio_test PRIVATE WIZ8_AUDIO_TEST_MP3="${PROJECT_SOURCE_DIR}/tests/native/tone.mp3")
catch_discover_tests(native_audio_test
    TEST_PREFIX "native_audio/"
    TEST_SPEC "~[.manual]"
    PROPERTIES ENVIRONMENT "SDL_AUDIO_DRIVER=wiz8-offline")

# Standalone SDL/Vulkan lifetime control for diagnosing host leak reports.
# Failures here are independent of game and SurRender ownership.
add_executable(native_gpu_lifecycle "${PROJECT_SOURCE_DIR}/tests/native/gpu_lifecycle.cpp")
target_link_libraries(native_gpu_lifecycle PRIVATE SDL3::SDL3)
# This is an SDL/driver diagnostic, not a game regression test.
# Keep its executable for manual leak investigations; don't run it in CTest.

add_executable(native_gpu_rendering_test native/gpu_rendering_test.cpp)
target_link_libraries(native_gpu_rendering_test PRIVATE SURRENDER wiz8_native_settings SDL3::SDL3)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME native_gpu_rendering COMMAND native_gpu_rendering_test)
    set_tests_properties(native_gpu_rendering PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=offscreen" TIMEOUT 30)
endif()

add_executable(native_movie_test "${PROJECT_SOURCE_DIR}/tests/native/movie_test.cpp")
target_compile_definitions(native_movie_test PRIVATE
    WIZ8_MOVIE_FIXTURE="${PROJECT_SOURCE_DIR}/tests/native/movie.mkv"
    WIZ8_MOVIE_GOLDEN="${PROJECT_SOURCE_DIR}/tests/native/movie.rgb555")
target_link_libraries(native_movie_test PRIVATE WIZ8_GAME_CORE SDL3::SDL3)
if(APPLE)
    target_link_options(native_movie_test PRIVATE -Wl,-dead_strip)
elseif(UNIX)
    target_link_options(native_movie_test PRIVATE -Wl,--gc-sections -Wl,-z,start-stop-gc)
endif()
add_test(NAME native_movies COMMAND native_movie_test)
set_tests_properties(native_movies PROPERTIES ENVIRONMENT "SDL_AUDIO_DRIVER=wiz8-offline")

add_executable(native_application_test native/application_test.cpp)
target_link_libraries(native_application_test PRIVATE WIZ8_GAME_CORE)
add_test(NAME native_application COMMAND native_application_test)
set_tests_properties(native_application PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy" TIMEOUT 10)

# Installed level/character integration; supplied assets and a display are required.
add_executable(native_world_graphics "${PROJECT_SOURCE_DIR}/tests/native/world_graphics.cpp")
target_link_libraries(native_world_graphics PRIVATE WIZ8_GAME_CORE SDL3::SDL3)

# Scenarios through the real game loop against installed retail assets
# (WIZ8_ASSET_ROOT); skipped when it is unset. bink_video.cpp is recompiled
# with WIZ8_RUNTIME_TESTS (as is native_helpers.cpp, the game clock) so their test
# hooks replace the archives' objects.
add_executable(native_runtime_test runtime/runtime_test.cpp
    "${PROJECT_SOURCE_DIR}/src/native/bink_video.cpp"
    "${PROJECT_SOURCE_DIR}/src/compat/native_helpers.cpp")
target_compile_definitions(native_runtime_test PRIVATE WIZ8_RUNTIME_TESTS=1)
target_include_directories(native_runtime_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/native")
target_link_libraries(native_runtime_test PRIVATE WIZ8_GAME_CORE SDL3::SDL3)
set(WIZ8_RUNTIME_SAVE_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/runtime-fixtures")
target_compile_definitions(native_runtime_test PRIVATE
    WIZ8_RUNTIME_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/runtime/fixtures"
    WIZ8_RUNTIME_SAVE_FIXTURE="${WIZ8_RUNTIME_SAVE_FIXTURE}")
foreach(scenario video video-seen main-menu-ui formation-ui game-start resource-lifecycle inventory-chest camp-spells quick-save combat npc-dialogue load-game random-play menu-save)
    # A fixed seed makes the game's random rolls repeatable.
    add_test(NAME runtime_${scenario} COMMAND native_runtime_test ${scenario} --rng-seed 1)
    set_tests_properties(runtime_${scenario} PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=offscreen;SDL_AUDIO_DRIVER=dummy"
        SKIP_REGULAR_EXPRESSION "WIZ8_ASSET_ROOT is not set" TIMEOUT 120
        RESOURCE_LOCK game_gpu LABELS runtime)
endforeach()
# The quick-save scenario writes a fixture consumed by the menu-driven load test.
# CTest arranges the dependency even when only runtime_load-game is selected.
set_tests_properties(runtime_quick-save PROPERTIES FIXTURES_SETUP runtime_save)
set_tests_properties(runtime_load-game PROPERTIES FIXTURES_REQUIRED runtime_save)

add_executable(native_resource_lifecycle_test native/resource_lifecycle_test.cpp)
target_link_libraries(native_resource_lifecycle_test PRIVATE WIZ8_GAME_CORE)
add_test(NAME native_resource_lifecycle COMMAND native_resource_lifecycle_test)

add_executable(native_serialized_mesh_test native/serialized_mesh_test.cpp)
target_include_directories(native_serialized_mesh_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/native")
target_link_libraries(native_serialized_mesh_test PRIVATE WIZ8_GAME_CORE)
add_test(NAME native_serialized_mesh COMMAND native_serialized_mesh_test)

add_executable(native_indexes_test native/indexes_test.cpp)
target_link_libraries(native_indexes_test PRIVATE WIZ8_GAME_CORE SURRENDER wiz8_native_settings)
if(APPLE)
    target_link_options(native_indexes_test PRIVATE -Wl,-dead_strip)
elseif(UNIX)
    target_link_options(native_indexes_test PRIVATE -Wl,--gc-sections -Wl,-z,start-stop-gc)
endif()
add_test(NAME native_indexes COMMAND native_indexes_test)

add_executable(native_collections_test "${PROJECT_SOURCE_DIR}/tests/native/collections_test.cpp")
target_link_libraries(native_collections_test PRIVATE WIZ8_GAME_CORE SDL3::SDL3)
if(APPLE)
    target_link_options(native_collections_test PRIVATE -Wl,-dead_strip)
elseif(UNIX)
    target_link_options(native_collections_test PRIVATE -Wl,--gc-sections -Wl,-z,start-stop-gc)
endif()
add_test(NAME native_collections COMMAND native_collections_test)

add_executable(native_random_test native/random_test.cpp)
target_link_libraries(native_random_test PRIVATE WIZ8_GAME_CORE)
if(APPLE)
    target_link_options(native_random_test PRIVATE -Wl,-dead_strip)
elseif(UNIX)
    target_link_options(native_random_test PRIVATE -Wl,--gc-sections -Wl,-z,start-stop-gc)
endif()
add_test(NAME native_random COMMAND native_random_test)

add_executable(native_resource_text_test native/resource_text_test.cpp)
target_link_libraries(native_resource_text_test PRIVATE WIZ8_GAME_CORE)
add_test(NAME native_resource_text COMMAND native_resource_text_test)
