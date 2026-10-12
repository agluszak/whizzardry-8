#include <cstdint>
#include <fmt/format.h>
#include <exception>
#include <string>

#include "wiz8/sr_api.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "wiz8/chunk.h"
#include "wiz8/engine_code/AmbientSound.h"
#include "wiz8/engine_code/Quality.h"
#include "wiz8/engine_code/Video2.h"

// GLOBAL: WIZ8 0x006850c8
W8GameSettings g_settings;
// GLOBAL: WIZ8 0x0061e184
static char g_config_file_name[8] = "Wiz8";
// GLOBAL: WIZ8 0x0061e18c
static char g_config_file_extension[] = "CFG";
constexpr std::uint32_t W8_CHUNK_SETTINGS = W8ChunkId("CNFG");
constexpr std::uint32_t W8_CHUNK_RENDER_OPTIONS = W8ChunkId("QLTY");
constexpr std::uint32_t W8_CHUNK_KEY_BINDINGS = W8ChunkId("KEYM");

// FUNCTION: WIZ8 0x0054b810
void LoadGameConfiguration(void)
{
    W8Chunk file;
    std::string path;
    bool loaded = false;
    bool reset = true;

    path = fmt::format("{}.{}", g_config_file_name, g_config_file_extension);
    ResetMGSKeyboardBindings();
    if (file.OpenRead(path)) {
        file.ForEachChunk([&](std::uint32_t id) {
            if (id == W8_CHUNK_SETTINGS) {
                if (file.CurrentChunkExtent() == sizeof(g_settings)) {
                    file.ReadValue(g_settings);
                    loaded = true;
                }
            } else if (id == W8_CHUNK_KEY_BINDINGS) {
                g_mgs_keyboard->Load(file.Stream(), false);
            } else if (id == W8_CHUNK_RENDER_OPTIONS) {
                LoadRenderOptions(file.Stream());
            }
        });
        file.Close();
    }
    if (loaded) {
        if (static_cast<std::uint32_t>(g_settings.combat_delay_ms) <= 5000u &&
            g_settings.text_display_delay_ms <= 5000u) {
            reset = false;
        } else {
            srAssertFail(
                "FALSE", "C:\\Projects\\Wizardry 8\\Local Code\\Configuration.cpp", 0xd9,
                fmt::format("LoadConfig: ERROR - Config file appears to be corrupted.  Delete {}",
                            path));
        }
    }
    if (reset) {
        ResetGameplaySettings();
        SaveGameConfiguration();
    }
    SetSoundEffectsVolume(g_settings.sound_effects_volume);
    SetMusicVolume(g_settings.music_volume);
    if (g_settings.gamma < 0.1f || g_settings.gamma > 2.0f) {
        g_settings.gamma = 1.0f;
    }
    SetDisplayGamma(g_settings.gamma);
}

// FUNCTION: WIZ8 0x0054b6d0
bool SaveGameConfiguration(void)
try
{
    W8Chunk file;
    std::string path;

    path = fmt::format("{}.{}", g_config_file_name, g_config_file_extension);
    if (!file.OpenWrite(path)) {
        return false;
    }
    file.WriteChunk(W8_CHUNK_SETTINGS, [&] { file.WriteValue(g_settings); });
    file.WriteChunk(W8_CHUNK_RENDER_OPTIONS, [&] { SaveRenderOptions(file.Stream()); });
    file.WriteChunk(W8_CHUNK_KEY_BINDINGS, [&] { g_mgs_keyboard->Save(file.Stream()); });
    file.Close();
    return true;
}
catch (const std::exception&) { return false; }
