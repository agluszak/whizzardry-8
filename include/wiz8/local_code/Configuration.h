#pragma once
#include <cstddef>
#include <span>
#include <vector>

#include "wiz8/difficulty.h"
#include "wiz8/layouts/main_game_screen.h"

enum W8CameraRotationMode {
    W8_CAMERA_ROTATION_SELECTED_CHARACTER = 0,
    W8_CAMERA_ROTATION_ALL_TARGETS = 1,
    W8_CAMERA_ROTATION_DISABLED = 2
};

enum W8CameraRotationStyle { W8_CAMERA_ROTATION_SNAP = 0, W8_CAMERA_ROTATION_SMOOTH = 1 };

void LoadGameConfiguration(void);
bool SaveGameConfiguration(void);
/* Bodies live in music_playlist.cpp (address-interleaved with that fragment). */
void SetMusicVolume(unsigned char volume);
bool IsMusicMuted(void);
void SetMusicMuted(unsigned char muted);

/* The persisted configuration. The configuration file's CNFG chunk stores it as a fixed
   0xa4-byte record (EncodeGameSettings); unnamed slots keep their bytes. */
struct W8GameSettings {
    bool mouselook_toggle;
    bool mouselook_smoothing;
    bool invert_mouse_y;
    bool ctrl_right_click_info;
    W8Difficulty difficulty;
    W8CameraRotationMode camera_rotation_mode;
    W8CameraRotationStyle camera_rotation_style;
    bool continuous_combat;
    bool numeric_hit_points;
    bool autoscroll_combat_messages;
    bool simplified_npc_interaction;
    bool verbose_combat_messages;
    bool autoswap_weapons;
    bool auto_advance_character;
    bool autotarget_spells;
    bool skill_increase_messages;
    int combat_delay_ms;
    float monster_movement_speed;
    unsigned int text_display_delay_ms;
    bool auto_save;
    bool monster_shadows;           /* options string 0x818 */
    bool smooth_monster_animations; /* options string 0x819 */
    bool smooth_world_animations;   /* options string 0x81a */
    bool pc_confirmations; /* options string 0x823 */
    bool pc_subtitles; /* options string 0x824 */
    unsigned char field_000;
    unsigned char unknown_002[0x4];
    /* The player's preferred main-game layout. An open interaction overrides
       it in the applied MainGameLayout without changing it. */
    W8MainUiMode main_ui_mode;
    bool tooltips_enabled;
    int continuous_combat_start_delay_ms;
    int tooltip_delay_ms;
    unsigned char formation_action_panel_preference;
    unsigned char formation_radar_map_preference;
    unsigned char formation_board_preference;
    unsigned char portraits_action_panel_preference;
    unsigned char unknown_02d[0x1];
    unsigned char sound_effects_volume;
    unsigned char music_volume;
    unsigned char voice_volume;
    unsigned char footstep_volume;
    unsigned char muted_sound_effects_volume;
    unsigned char muted_music_volume;
    unsigned char muted_voice_volume;
    unsigned char field_035;
    unsigned char field_03b;
    float legacy_gamma; // Retained CNFG slot; gamma is not a runtime setting.
    bool intro_seen;
    unsigned char field_047;
    unsigned char unknown_051[0x53];
};

extern W8GameSettings g_settings;

/* The CNFG chunk of the configuration file. Decoding throws std::runtime_error unless the
   payload is exactly one record. */
std::vector<std::byte> EncodeGameSettings(const W8GameSettings& settings);
W8GameSettings DecodeGameSettings(std::span<const std::byte> payload);
