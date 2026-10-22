#include "wiz8/local_code/Configuration.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "asset_paths.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/chunk.h"
#include "wiz8/difficulty.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "wiz8/render/quality.h"
#include "wiz8/retail_text_records.h"

W8GameSettings g_settings;
static char g_config_file_name[8] = "Wiz8";
static char g_config_file_extension[] = "CFG";
constexpr unsigned int W8_CHUNK_SETTINGS = W8ChunkId("CNFG");
constexpr unsigned int W8_CHUNK_RENDER_OPTIONS = W8ChunkId("QLTY");
constexpr unsigned int W8_CHUNK_KEY_BINDINGS = W8ChunkId("KEYM");

namespace wiz8::retail {
// The CNFG record.
#pragma pack(push, 1)
struct GameSettings {
    std::uint8_t field_000;
    std::uint8_t numeric_hit_points;
    std::uint8_t unknown_002[0x4];
    std::int32_t main_ui_mode;
    std::uint8_t continuous_combat;
    std::uint8_t auto_advance_character;
    std::uint8_t tooltips_enabled;
    std::int32_t difficulty;
    std::uint32_t text_display_delay_ms;
    std::int32_t combat_delay_ms;
    std::int32_t continuous_combat_start_delay_ms;
    std::int32_t camera_rotation_mode;
    std::int32_t camera_rotation_style;
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
    float legacy_gamma;
    std::uint8_t pc_confirmations;
    std::uint8_t mouselook_toggle;
    std::uint8_t pc_subtitles;
    std::uint8_t mouselook_smoothing;
    std::uint8_t verbose_combat_messages;
    std::uint8_t auto_save;
    std::uint8_t intro_seen;
    std::uint8_t field_047;
    std::uint8_t monster_shadows;
    std::uint8_t smooth_monster_animations;
    std::uint8_t smooth_world_animations;
    std::uint8_t skill_increase_messages;
    std::uint8_t ctrl_right_click_info;
    std::uint8_t autoswap_weapons;
    std::uint8_t autotarget_spells;
    std::uint8_t autoscroll_combat_messages;
    std::uint8_t simplified_npc_interaction;
    std::uint8_t unknown_051[0x53];
};
#pragma pack(pop)
static_assert(sizeof(GameSettings) == 0xa4);

template<> struct Record<W8GameSettings> {
    using Disk = GameSettings;
    template<class Runtime, class Bytes, class Value, class Text>
    static void fields(Runtime& runtime, Bytes bytes, Value value, Text)
    {
        const auto at = [&](std::size_t offset, std::size_t size) {
            return bytes.subspan(offset, size);
        };
        value(&runtime.field_000, at(offsetof(Disk, field_000), sizeof(Disk::field_000)));
        byte_boolean(&runtime.numeric_hit_points,
                     at(offsetof(Disk, numeric_hit_points), sizeof(Disk::numeric_hit_points)));
        value(&runtime.unknown_002, at(offsetof(Disk, unknown_002), sizeof(Disk::unknown_002)));
        enum_value<std::int32_t>(&runtime.main_ui_mode,
                                 at(offsetof(Disk, main_ui_mode), sizeof(Disk::main_ui_mode)));
        byte_boolean(&runtime.continuous_combat,
                     at(offsetof(Disk, continuous_combat), sizeof(Disk::continuous_combat)));
        byte_boolean(&runtime.auto_advance_character, at(offsetof(Disk, auto_advance_character),
                                                         sizeof(Disk::auto_advance_character)));
        byte_boolean(&runtime.tooltips_enabled,
                     at(offsetof(Disk, tooltips_enabled), sizeof(Disk::tooltips_enabled)));
        enum_value<std::int32_t>(&runtime.difficulty,
                                 at(offsetof(Disk, difficulty), sizeof(Disk::difficulty)));
        value(&runtime.text_display_delay_ms,
              at(offsetof(Disk, text_display_delay_ms), sizeof(Disk::text_display_delay_ms)));
        value(&runtime.combat_delay_ms,
              at(offsetof(Disk, combat_delay_ms), sizeof(Disk::combat_delay_ms)));
        value(&runtime.continuous_combat_start_delay_ms,
              at(offsetof(Disk, continuous_combat_start_delay_ms),
                 sizeof(Disk::continuous_combat_start_delay_ms)));
        enum_value<std::int32_t>(
            &runtime.camera_rotation_mode,
            at(offsetof(Disk, camera_rotation_mode), sizeof(Disk::camera_rotation_mode)));
        enum_value<std::int32_t>(
            &runtime.camera_rotation_style,
            at(offsetof(Disk, camera_rotation_style), sizeof(Disk::camera_rotation_style)));
        value(&runtime.tooltip_delay_ms,
              at(offsetof(Disk, tooltip_delay_ms), sizeof(Disk::tooltip_delay_ms)));
        value(&runtime.formation_action_panel_preference,
              at(offsetof(Disk, formation_action_panel_preference),
                 sizeof(Disk::formation_action_panel_preference)));
        value(&runtime.formation_radar_map_preference,
              at(offsetof(Disk, formation_radar_map_preference),
                 sizeof(Disk::formation_radar_map_preference)));
        value(&runtime.formation_board_preference, at(offsetof(Disk, formation_board_preference),
                                                      sizeof(Disk::formation_board_preference)));
        value(&runtime.portraits_action_panel_preference,
              at(offsetof(Disk, portraits_action_panel_preference),
                 sizeof(Disk::portraits_action_panel_preference)));
        value(&runtime.unknown_02d, at(offsetof(Disk, unknown_02d), sizeof(Disk::unknown_02d)));
        value(&runtime.sound_effects_volume,
              at(offsetof(Disk, sound_effects_volume), sizeof(Disk::sound_effects_volume)));
        value(&runtime.music_volume, at(offsetof(Disk, music_volume), sizeof(Disk::music_volume)));
        value(&runtime.voice_volume, at(offsetof(Disk, voice_volume), sizeof(Disk::voice_volume)));
        value(&runtime.footstep_volume,
              at(offsetof(Disk, footstep_volume), sizeof(Disk::footstep_volume)));
        value(&runtime.muted_sound_effects_volume, at(offsetof(Disk, muted_sound_effects_volume),
                                                      sizeof(Disk::muted_sound_effects_volume)));
        value(&runtime.muted_music_volume,
              at(offsetof(Disk, muted_music_volume), sizeof(Disk::muted_music_volume)));
        value(&runtime.muted_voice_volume,
              at(offsetof(Disk, muted_voice_volume), sizeof(Disk::muted_voice_volume)));
        value(&runtime.field_035, at(offsetof(Disk, field_035), sizeof(Disk::field_035)));
        byte_boolean(&runtime.invert_mouse_y,
                     at(offsetof(Disk, invert_mouse_y), sizeof(Disk::invert_mouse_y)));
        value(&runtime.monster_movement_speed,
              at(offsetof(Disk, monster_movement_speed), sizeof(Disk::monster_movement_speed)));
        value(&runtime.field_03b, at(offsetof(Disk, field_03b), sizeof(Disk::field_03b)));
        value(&runtime.legacy_gamma, at(offsetof(Disk, legacy_gamma), sizeof(Disk::legacy_gamma)));
        byte_boolean(&runtime.pc_confirmations,
                     at(offsetof(Disk, pc_confirmations), sizeof(Disk::pc_confirmations)));
        byte_boolean(&runtime.mouselook_toggle,
                     at(offsetof(Disk, mouselook_toggle), sizeof(Disk::mouselook_toggle)));
        byte_boolean(&runtime.pc_subtitles,
                     at(offsetof(Disk, pc_subtitles), sizeof(Disk::pc_subtitles)));
        byte_boolean(&runtime.mouselook_smoothing,
                     at(offsetof(Disk, mouselook_smoothing), sizeof(Disk::mouselook_smoothing)));
        byte_boolean(&runtime.verbose_combat_messages, at(offsetof(Disk, verbose_combat_messages),
                                                          sizeof(Disk::verbose_combat_messages)));
        byte_boolean(&runtime.auto_save, at(offsetof(Disk, auto_save), sizeof(Disk::auto_save)));
        byte_boolean(&runtime.intro_seen, at(offsetof(Disk, intro_seen), sizeof(Disk::intro_seen)));
        value(&runtime.field_047, at(offsetof(Disk, field_047), sizeof(Disk::field_047)));
        byte_boolean(&runtime.monster_shadows,
                     at(offsetof(Disk, monster_shadows), sizeof(Disk::monster_shadows)));
        byte_boolean(
            &runtime.smooth_monster_animations,
            at(offsetof(Disk, smooth_monster_animations), sizeof(Disk::smooth_monster_animations)));
        byte_boolean(&runtime.smooth_world_animations, at(offsetof(Disk, smooth_world_animations),
                                                          sizeof(Disk::smooth_world_animations)));
        byte_boolean(&runtime.skill_increase_messages, at(offsetof(Disk, skill_increase_messages),
                                                          sizeof(Disk::skill_increase_messages)));
        byte_boolean(&runtime.ctrl_right_click_info, at(offsetof(Disk, ctrl_right_click_info),
                                                        sizeof(Disk::ctrl_right_click_info)));
        byte_boolean(&runtime.autoswap_weapons,
                     at(offsetof(Disk, autoswap_weapons), sizeof(Disk::autoswap_weapons)));
        byte_boolean(&runtime.autotarget_spells,
                     at(offsetof(Disk, autotarget_spells), sizeof(Disk::autotarget_spells)));
        byte_boolean(&runtime.autoscroll_combat_messages,
                     at(offsetof(Disk, autoscroll_combat_messages),
                        sizeof(Disk::autoscroll_combat_messages)));
        byte_boolean(&runtime.simplified_npc_interaction,
                     at(offsetof(Disk, simplified_npc_interaction),
                        sizeof(Disk::simplified_npc_interaction)));
        value(&runtime.unknown_051, at(offsetof(Disk, unknown_051), sizeof(Disk::unknown_051)));
    }
};
} // namespace wiz8::retail

std::vector<std::byte> EncodeGameSettings(const W8GameSettings& settings)
{
    const auto image = wiz8::retail::encode(settings);
    return std::vector<std::byte>(image.begin(), image.end());
}

W8GameSettings DecodeGameSettings(std::span<const std::byte> payload)
{
    if (payload.size() != sizeof(wiz8::retail::GameSettings))
        throw std::runtime_error("invalid settings record");
    W8GameSettings settings{};
    wiz8::retail::decode(payload, settings);
    return settings;
}

void LoadGameConfiguration(void)
{
    W8Chunk file;
    std::string path;
    bool loaded = false;
    bool reset = true;

    path = fmt::format("{}.{}", g_config_file_name, g_config_file_extension);
    ResetMGSKeyboardBindings();
    if (file.OpenRead(wiz8::user_path("config/" + path))) {
        file.ForEachChunk([&](unsigned int id) {
            if (id == W8_CHUNK_SETTINGS) {
                std::vector<std::byte> payload(static_cast<std::size_t>(file.RemainingBytes()));
                file.ReadBytes(payload.data(), payload.size());
                try {
                    g_settings = DecodeGameSettings(payload);
                    loaded = true;
                } catch (const std::runtime_error&) {
                    // A record of another size is not this version's; defaults replace it.
                }
            } else if (id == W8_CHUNK_KEY_BINDINGS) {
                g_mgs_keyboard.Load(file, false);
            } else if (id == W8_CHUNK_RENDER_OPTIONS) {
                LoadRenderOptions(file);
            }
        });
        file.Close();
    }
    // Out-of-range delays or difficulty mean a corrupt file; fall back to the defaults.
    if (loaded && static_cast<unsigned int>(g_settings.combat_delay_ms) <= 5000u &&
        g_settings.text_display_delay_ms <= 5000u &&
        static_cast<unsigned int>(g_settings.difficulty) <= W8_DIFFICULTY_EXPERT) {
        reset = false;
    }
    if (reset) {
        ResetGameplaySettings();
        SaveGameConfiguration();
    }
    SetSoundEffectsVolume(g_settings.sound_effects_volume);
    wiz8::sound::set_channel_volume(wiz8::sound::VolumeChannel::voice, g_settings.voice_volume);
    wiz8::sound::set_channel_volume(wiz8::sound::VolumeChannel::footsteps,
                                    g_settings.footstep_volume);
    SetMusicVolume(g_settings.music_volume);
}

bool SaveGameConfiguration(void)
try {
    W8Chunk file;
    std::string path;

    path = fmt::format("{}.{}", g_config_file_name, g_config_file_extension);
    if (!file.OpenWrite(wiz8::user_path("config/" + path))) {
        return false;
    }
    file.WriteChunk(W8_CHUNK_SETTINGS, [&] {
        const auto record = EncodeGameSettings(g_settings);
        file.WriteBytes(record.data(), record.size());
    });
    file.WriteChunk(W8_CHUNK_RENDER_OPTIONS, [&] { SaveRenderOptions(file.WriteStream()); });
    file.WriteChunk(W8_CHUNK_KEY_BINDINGS, [&] { g_mgs_keyboard.Save(file.WriteStream()); });
    file.Close();
    return true;
} catch (const std::exception&) {
    return false;
}
