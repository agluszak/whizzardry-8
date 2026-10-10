add_executable(native_compression_test "${PROJECT_SOURCE_DIR}/tests/native/compression_test.cpp")
target_link_libraries(native_compression_test PRIVATE WIZ8_SGP)
add_test(NAME native_compression COMMAND native_compression_test)

add_executable(native_images_test native/images_test.cpp)
target_link_libraries(native_images_test PRIVATE WIZ8_SGP)
add_test(NAME native_images COMMAND native_images_test)

add_executable(native_files_test "${PROJECT_SOURCE_DIR}/tests/native/files_test.cpp")
target_link_libraries(native_files_test PRIVATE WIZ8_GAME_CORE WIZ8_SGP SURRENDER)
add_test(NAME native_files COMMAND native_files_test)

add_executable(native_strings_test native/strings_test.cpp)
target_link_libraries(native_strings_test PRIVATE SURRENDER wiz8_native_settings)
add_test(NAME native_strings COMMAND native_strings_test)

add_executable(native_events_test "${PROJECT_SOURCE_DIR}/tests/native/events_test.cpp")
target_link_libraries(native_events_test PRIVATE WIZ8_SGP SDL3::SDL3)
add_test(NAME native_events COMMAND native_events_test)
set_tests_properties(native_events PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy" TIMEOUT 10)

add_executable(native_imports_test "${PROJECT_SOURCE_DIR}/tests/native/imports_test.cpp")
target_link_libraries(native_imports_test PRIVATE SURRENDER wiz8_native_settings)
add_test(NAME native_imports COMMAND native_imports_test)

add_executable(native_image_io_test native/image_io_test.cpp)
target_link_libraries(native_image_io_test PRIVATE SURRENDER wiz8_native_settings)
add_test(NAME native_image_io COMMAND native_image_io_test)

add_executable(native_renderer_platform_test native/renderer_platform_test.cpp)
target_link_libraries(native_renderer_platform_test PRIVATE SURRENDER wiz8_native_settings)
add_test(NAME native_renderer_platform COMMAND native_renderer_platform_test)
set_tests_properties(native_renderer_platform PROPERTIES TIMEOUT 10)

add_executable(native_surrender_test native/surrender_test.cpp)
target_link_libraries(native_surrender_test PRIVATE SURRENDER wiz8_native_settings)
add_test(NAME native_surrender COMMAND native_surrender_test)

add_executable(native_crt_test "${PROJECT_SOURCE_DIR}/tests/native/crt_test.cpp")
target_link_libraries(native_crt_test PRIVATE wiz8_native_settings)
add_test(NAME native_crt COMMAND native_crt_test)

add_executable(native_pointer_test "${PROJECT_SOURCE_DIR}/tests/native/pointer_test.cpp")
target_link_libraries(native_pointer_test PRIVATE wiz8_native_settings)
add_test(NAME native_pointer COMMAND native_pointer_test)

add_executable(native_save_records_test "${PROJECT_SOURCE_DIR}/tests/native/save_records_test.cpp")
target_link_libraries(native_save_records_test PRIVATE WIZ8_GAME_CORE WIZ8_SGP WIZ8_GAME_CORE SDL3::SDL3)
if(APPLE)
    target_link_options(native_save_records_test PRIVATE -Wl,-dead_strip)
elseif(UNIX)
    target_link_options(native_save_records_test PRIVATE -Wl,--gc-sections -Wl,-z,start-stop-gc)
endif()
add_test(NAME native_save_records COMMAND native_save_records_test)

add_executable(native_jpeg_transfer_test "${PROJECT_SOURCE_DIR}/tests/native/jpeg_transfer_test.cpp")
target_include_directories(native_jpeg_transfer_test PRIVATE "${PROJECT_SOURCE_DIR}/src/srext_jpegimporter")
target_compile_definitions(native_jpeg_transfer_test PRIVATE WIZ8_IMAGE_IMPORT_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/native/image_import")
target_link_libraries(native_jpeg_transfer_test PRIVATE SURRENDER wiz8_native_settings SDL3_image::SDL3_image)
add_test(NAME native_jpeg_transfer COMMAND native_jpeg_transfer_test)

add_executable(native_virtual_image_transfer_test native/image_import/virtual_transfer_test.cpp)
target_include_directories(native_virtual_image_transfer_test PRIVATE "${PROJECT_SOURCE_DIR}/src/srext_jpegimporter")
target_compile_definitions(native_virtual_image_transfer_test PRIVATE WIZ8_IMAGE_IMPORT_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/native/image_import")
target_link_libraries(native_virtual_image_transfer_test PRIVATE WIZ8_GAME_CORE WIZ8_SGP WIZ8_GAME_CORE SDL3_image::SDL3_image)
if(APPLE)
    target_link_options(native_virtual_image_transfer_test PRIVATE -Wl,-dead_strip)
elseif(UNIX)
    target_link_options(native_virtual_image_transfer_test PRIVATE -Wl,--gc-sections -Wl,-z,start-stop-gc)
endif()
add_test(NAME native_virtual_image_transfer COMMAND native_virtual_image_transfer_test)

add_executable(native_surface_test native/surface_test.cpp)
target_link_libraries(native_surface_test PRIVATE WIZ8_SGP)
add_test(NAME native_surfaces COMMAND native_surface_test)

add_executable(native_owner_test native/owner_test.cpp)
target_link_libraries(native_owner_test PRIVATE WIZ8_GAME_CORE WIZ8_SGP WIZ8_GAME_CORE SDL3::SDL3)
if(APPLE)
    target_link_options(native_owner_test PRIVATE -Wl,-dead_strip)
elseif(UNIX)
    target_link_options(native_owner_test PRIVATE -Wl,--gc-sections -Wl,-z,start-stop-gc)
endif()
add_test(NAME native_owners COMMAND native_owner_test)
set_tests_properties(native_owners PROPERTIES TIMEOUT 10)
foreach(mode plain message)
    add_test(NAME native_assertion_${mode}
        COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:native_owner_test>" "-DMODE=${mode}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/assertion_failure.cmake")
endforeach()

add_executable(native_pixel_convert_test native/pixel_convert_test.cpp)
target_link_libraries(native_pixel_convert_test PRIVATE SURRENDER wiz8_native_settings SDL3::SDL3)
add_test(NAME native_pixel_convert COMMAND native_pixel_convert_test)

add_executable(native_vector_math_test native/vector_math_test.cpp)
target_link_libraries(native_vector_math_test PRIVATE SURRENDER wiz8_native_settings)
add_test(NAME native_vector_math COMMAND native_vector_math_test)

add_executable(native_blitter_test "${PROJECT_SOURCE_DIR}/tests/native/blitter_test.cpp")
target_link_libraries(native_blitter_test PRIVATE WIZ8_SGP)
add_test(NAME native_blitters COMMAND native_blitter_test
    "${PROJECT_SOURCE_DIR}/tests/native/blitters_expected.txt")

# Check first-party objects, not SDL's statically linked host-width code.
if(UNIX)
    add_test(NAME native_no_glibc_wide_strings
        COMMAND "${CMAKE_COMMAND}" "-DNM=${CMAKE_NM}"
            "-DOBJECT_DIRS=${PROJECT_BINARY_DIR}/CMakeFiles/wiz8_compat.dir;${PROJECT_BINARY_DIR}/src/surrender/CMakeFiles/SURRENDER.dir;${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/native_crt_test.dir"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/no_host_wide_strings.cmake")
endif()

add_executable(native_game_graphics "${PROJECT_SOURCE_DIR}/tests/native/game_graphics.cpp")
target_compile_definitions(native_game_graphics PRIVATE
    WIZ8_MOVIE_FIXTURE="${PROJECT_SOURCE_DIR}/tests/native/movie.mkv")
target_link_libraries(native_game_graphics PRIVATE WIZ8_GAME_CORE WIZ8_SGP WIZ8_GAME_CORE SDL3::SDL3)
if(APPLE)
    target_link_options(native_game_graphics PRIVATE -Wl,-dead_strip)
elseif(UNIX)
    # Discard unused ASan global-registration sections alongside unused game
    # functions in this focused graphics harness.
    target_link_options(native_game_graphics PRIVATE -Wl,--gc-sections -Wl,-z,start-stop-gc)
endif()
# Uses installed retail assets, supplied through WIZ8_ASSET_ROOT; run explicitly.

add_executable(native_audio_test "${PROJECT_SOURCE_DIR}/tests/native/audio_test.cpp")
target_link_libraries(native_audio_test PRIVATE WIZ8_SGP)
target_compile_definitions(native_audio_test PRIVATE WIZ8_AUDIO_TEST_MP3="${PROJECT_SOURCE_DIR}/tests/native/tone.mp3")
add_test(NAME native_audio COMMAND native_audio_test)

# Standalone SDL/Vulkan lifetime control for diagnosing host leak reports.
# Run manually: failures here are independent of game and SurRender ownership.
add_executable(native_gpu_lifecycle "${PROJECT_SOURCE_DIR}/tests/native/gpu_lifecycle.cpp")
target_link_libraries(native_gpu_lifecycle PRIVATE SDL3::SDL3)

add_executable(native_movie_test "${PROJECT_SOURCE_DIR}/tests/native/movie_test.cpp")
target_compile_definitions(native_movie_test PRIVATE
    WIZ8_MOVIE_FIXTURE="${PROJECT_SOURCE_DIR}/tests/native/movie.mkv"
    WIZ8_MOVIE_GOLDEN="${PROJECT_SOURCE_DIR}/tests/native/movie.rgb555")
target_link_libraries(native_movie_test PRIVATE WIZ8_GAME_CORE WIZ8_SGP WIZ8_GAME_CORE SDL3::SDL3)
if(APPLE)
    target_link_options(native_movie_test PRIVATE -Wl,-dead_strip)
elseif(UNIX)
    target_link_options(native_movie_test PRIVATE -Wl,--gc-sections -Wl,-z,start-stop-gc)
endif()
add_test(NAME native_movies COMMAND native_movie_test)
set_tests_properties(native_audio native_movies PROPERTIES ENVIRONMENT "SDL_AUDIO_DRIVER=wiz8-offline")

# Installed level/character integration; supplied assets and a display are required.
add_executable(native_world_graphics "${PROJECT_SOURCE_DIR}/tests/native/world_graphics.cpp")
target_link_libraries(native_world_graphics PRIVATE WIZ8_GAME_CORE WIZ8_SGP WIZ8_GAME_CORE SDL3::SDL3)
