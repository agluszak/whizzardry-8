#include "wiz8/local_code/Configuration.h"

#include <exception>
#include <string>

#include <fmt/format.h>

#include "asset_paths.h"
#include "surrender/srDebug.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/chunk.h"
#include "wiz8/engine_code/Quality.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_screens/MGSKeyboard.h"

W8GameSettings g_settings;
static char g_config_file_name[8] = "Wiz8";
static char g_config_file_extension[] = "CFG";
constexpr unsigned int W8_CHUNK_SETTINGS = W8ChunkId("CNFG");
constexpr unsigned int W8_CHUNK_RENDER_OPTIONS = W8ChunkId("QLTY");
constexpr unsigned int W8_CHUNK_KEY_BINDINGS = W8ChunkId("KEYM");

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
                if (file.CurrentChunkExtent() == sizeof(g_settings)) {
                    file.ReadValue(g_settings);
                    loaded = true;
                }
            } else if (id == W8_CHUNK_KEY_BINDINGS) {
                g_mgs_keyboard.Load(file, false);
            } else if (id == W8_CHUNK_RENDER_OPTIONS) {
                LoadRenderOptions(file);
            }
        });
        file.Close();
    }
    if (loaded) {
        if (static_cast<unsigned int>(g_settings.combat_delay_ms) <= 5000u &&
            g_settings.text_display_delay_ms <= 5000u) {
            reset = false;
        } else {
            Fail("FALSE",
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
}

bool SaveGameConfiguration(void)
try {
    W8Chunk file;
    std::string path;

    path = fmt::format("{}.{}", g_config_file_name, g_config_file_extension);
    if (!file.OpenWrite(wiz8::user_path("config/" + path))) {
        return false;
    }
    file.WriteChunk(W8_CHUNK_SETTINGS, [&] { file.WriteValue(g_settings); });
    file.WriteChunk(W8_CHUNK_RENDER_OPTIONS, [&] { SaveRenderOptions(file.WriteStream()); });
    file.WriteChunk(W8_CHUNK_KEY_BINDINGS, [&] { g_mgs_keyboard.Save(file.WriteStream()); });
    file.Close();
    return true;
} catch (const std::exception&) {
    return false;
}
