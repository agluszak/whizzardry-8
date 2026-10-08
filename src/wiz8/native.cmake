# Compile the recovered game core. Platform units join when their native
# implementations are ready; this archive is not a runnable game executable.
include("${PROJECT_SOURCE_DIR}/src/wiz8/sources.cmake")
set(WIZ8_NATIVE_GAME_UNITS ${WIZ8_SOURCE_UNITS})
list(REMOVE_ITEM WIZ8_NATIVE_GAME_UNITS
    src/wiz8/imports/mss.cpp
    src/wiz8/engine_code/Bink.cpp
    src/wiz8/engine_code/Video2.cpp
)
add_library(WIZ8_GAME_CORE STATIC ${WIZ8_NATIVE_GAME_UNITS})
target_compile_definitions(WIZ8_GAME_CORE PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(WIZ8_GAME_CORE PUBLIC wiz8_native_settings WIZ8_SGP SURRENDER)
