add_executable(native_compression_test "${PROJECT_SOURCE_DIR}/tests/native/compression_test.cpp")
target_link_libraries(native_compression_test PRIVATE WIZ8_SGP)
add_test(NAME native_compression COMMAND native_compression_test)

add_executable(native_files_test "${PROJECT_SOURCE_DIR}/tests/native/files_test.cpp")
target_link_libraries(native_files_test PRIVATE WIZ8_SGP SURRENDER)
add_test(NAME native_files COMMAND native_files_test)

add_executable(native_events_test "${PROJECT_SOURCE_DIR}/tests/native/events_test.cpp")
target_link_libraries(native_events_test PRIVATE WIZ8_SGP SDL3::SDL3)
add_test(NAME native_events COMMAND native_events_test)
set_tests_properties(native_events PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy" TIMEOUT 10)

add_executable(native_imports_test "${PROJECT_SOURCE_DIR}/tests/native/imports_test.cpp")
target_link_libraries(native_imports_test PRIVATE SURRENDER wiz8_native_settings)
add_test(NAME native_imports COMMAND native_imports_test)

add_library(native_loader_fixture MODULE native/loader_fixture.cpp)
add_executable(native_renderer_platform_test native/renderer_platform_test.cpp)
target_link_libraries(native_renderer_platform_test PRIVATE SURRENDER wiz8_native_settings)
target_compile_definitions(native_renderer_platform_test PRIVATE
    WIZ8_PLATFORM_LIBRARY="$<TARGET_FILE:native_loader_fixture>")
add_test(NAME native_renderer_platform COMMAND native_renderer_platform_test)
set_tests_properties(native_renderer_platform PROPERTIES TIMEOUT 10)

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

add_executable(native_jpeg_transfer_test "${PROJECT_SOURCE_DIR}/tests/native/jpeg_transfer_test.cpp"
    "${PROJECT_SOURCE_DIR}/src/srext_jpegimporter/plugin.cpp" "${PROJECT_SOURCE_DIR}/src/srext_jpegimporter/surface_transfer.cpp")
target_include_directories(native_jpeg_transfer_test PRIVATE "${PROJECT_SOURCE_DIR}/src/srext_jpegimporter")
target_link_libraries(native_jpeg_transfer_test PRIVATE SURRENDER wiz8_native_settings)
add_test(NAME native_jpeg_transfer COMMAND native_jpeg_transfer_test)

add_executable(native_blitter_test "${PROJECT_SOURCE_DIR}/tests/native/blitter_test.cpp")
target_link_libraries(native_blitter_test PRIVATE WIZ8_SGP)
add_test(NAME native_blitters COMMAND native_blitter_test
    "${PROJECT_SOURCE_DIR}/tests/native/blitters_expected.txt")

# Check first-party objects, not SDL/miniaudio's statically linked host-width code.
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

# Installed level/character integration; supplied assets and a display are required.
add_executable(native_world_graphics "${PROJECT_SOURCE_DIR}/tests/native/world_graphics.cpp")
target_link_libraries(native_world_graphics PRIVATE WIZ8_GAME_CORE WIZ8_SGP WIZ8_GAME_CORE SDL3::SDL3)
