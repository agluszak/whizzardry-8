#pragma once

#include <cstdint>
#include "wiz8/difficulty.h"
#include "wiz8/layouts/main_game_screen.h"
#include "compat/compiler.h"

enum W8CameraRotationMode {
    W8_CAMERA_ROTATION_SELECTED_CHARACTER = 0,
    W8_CAMERA_ROTATION_ALL_TARGETS = 1,
    W8_CAMERA_ROTATION_DISABLED = 2
};

enum W8CameraRotationStyle { W8_CAMERA_ROTATION_SNAP = 0, W8_CAMERA_ROTATION_SMOOTH = 1 };

void LoadGameConfiguration(void);
bool SaveGameConfiguration(void);
/* Bodies live in music_playlist.cpp (address-interleaved with that fragment). */
void SetMusicVolume(std::uint8_t volume);
bool IsMusicMuted(void);
void SetMusicMuted(bool muted);

/* The persisted configuration block. */

#pragma pack(push, 1)
struct W8GameSettings {
    std::uint8_t field_000;
    std::uint8_t numeric_hit_points;
    std::uint8_t unknown_002[0x4];
    /* Mirrors W8LevelRuntimeBlock::main_ui_mode. ApplyMainGameModeFlag
       writes both together; NONE is the transient park Screens uses while it
       re-raises panels on main-game enter. */
    W8MainUiMode main_ui_mode;
    std::uint8_t continuous_combat;
    std::uint8_t auto_advance_character;
    bool tooltips_enabled;
    W8Difficulty difficulty;
    std::uint32_t text_display_delay_ms;
    std::int32_t combat_delay_ms;
    std::int32_t continuous_combat_start_delay_ms;
    W8CameraRotationMode camera_rotation_mode;
    W8CameraRotationStyle camera_rotation_style;
    std::int32_t tooltip_delay_ms;
    std::uint8_t formation_action_panel_preference;
    std::uint8_t formation_radar_map_preference;
    std::uint8_t formation_board_preference;
    std::uint8_t portraits_action_panel_preference;
    std::uint8_t unknown_02d[0x1];
    std::uint8_t sound_effects_volume;
    std::uint8_t music_volume;
    std::uint8_t voice_volume;
    std::uint8_t footstep_volume;
    std::uint8_t muted_sound_effects_volume;
    std::uint8_t muted_music_volume;
    std::uint8_t muted_voice_volume;
    std::uint8_t field_035;
    std::uint8_t invert_mouse_y;
    float monster_movement_speed;
    std::uint8_t field_03b;
    float gamma;
    std::uint8_t pc_confirmations; /* options string 0x823 */
    std::uint8_t mouselook_toggle;
    std::uint8_t pc_subtitles; /* options string 0x824 */
    std::uint8_t mouselook_smoothing;
    std::uint8_t verbose_combat_messages;
    std::uint8_t auto_save;
    bool intro_seen;
    std::uint8_t field_047;
    std::uint8_t monster_shadows;           /* options string 0x818 */
    std::uint8_t smooth_monster_animations; /* options string 0x819 */
    std::uint8_t smooth_world_animations;   /* options string 0x81a */
    std::uint8_t skill_increase_messages;
    std::uint8_t ctrl_right_click_info;
    std::uint8_t autoswap_weapons;
    std::uint8_t autotarget_spells;
    std::uint8_t autoscroll_combat_messages;
    std::uint8_t simplified_npc_interaction;
    std::uint8_t unknown_051[0x53];
};
#pragma pack(pop)

static_assert(sizeof(W8GameSettings) == 0xa4, "W8GameSettings_must_be_0xa4");

extern W8GameSettings g_settings;
