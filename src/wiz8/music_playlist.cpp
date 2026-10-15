#include "wiz8/music_playlist.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "audio_resources.h"
#include "compat/native.h"
#include "local_screens/Screens.h"
#include "random.h"
#include "soundman.h"
#include "wiz8/ascii.h"
#include "wiz8/engine_code/stScript.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/ui_input.h"
#include "wiz8/xstatus.h"

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

// GLOBAL: WIZ8 0x0060aae0
wiz8::sound::Playback g_music_sample_handle = nullptr;
// GLOBAL: WIZ8 0x0065BA74
static stScript* g_music_playlist;
// GLOBAL: WIZ8 0x0065BA78
static uint64_t g_music_playlist_tick;
// GLOBAL: WIZ8 0x0065BA7E
bool g_music_playlist_active;
// GLOBAL: WIZ8 0x0065BA80
int g_music_playlist_weight_total;
// GLOBAL: WIZ8 0x0065BA84
int g_music_playlist_track_count;
// GLOBAL: WIZ8 0x0060AAE4
static unsigned char g_music_fade = 1;
// GLOBAL: WIZ8 0x0060AAE5
static bool g_music_force_next = true;
// GLOBAL: WIZ8 0x0060AAE8
int g_music_pause_min_seconds = 20;
// GLOBAL: WIZ8 0x0060AAEC
int g_music_pause_max_seconds = 60;
// GLOBAL: WIZ8 0x0060AAF0
int g_music_pause_chance_percent = 30;

// FUNCTION: WIZ8 0x0048fe50
void SetMusicVolume(unsigned char volume)
{
    g_settings.music_volume = volume;
    if (g_music_sample_handle != nullptr) {
        wiz8::sound::set_volume(g_music_sample_handle, volume);
    }
}

// FUNCTION: WIZ8 0x0048fe80
bool IsMusicMuted(void)
{
    return g_settings.muted_music_volume != 0xff;
}

// FUNCTION: WIZ8 0x0048fe90
void SetMusicMuted(unsigned char muted)
{
    if (muted != 0) {
        if (g_settings.muted_music_volume == 0xff) {
            g_settings.muted_music_volume = g_settings.music_volume;
            SetMusicVolume(0);
        }
    } else if (g_settings.muted_music_volume != 0xff) {
        SetMusicVolume(g_settings.muted_music_volume);
        g_settings.muted_music_volume = 0xff;
    }
}

// FUNCTION: WIZ8 0x00490180
bool IsCurrentMusicPlaylist(const char* playlist)
{
    return wiz8::iequals(playlist, g_music_playlist->getName().c_str());
}

// FUNCTION: WIZ8 0x0048f940
unsigned char InitializeMusicPlaylist(void)
{
    g_music_playlist = new stScript();
    if (g_music_playlist) {
        g_music_playlist->setName("Music Playlist");
    }
    g_music_playlist_tick = w8_get_ticks();
    g_music_pause_min_seconds = 0;
    g_music_pause_max_seconds = 0;
    g_music_pause_chance_percent = 0;
    return g_music_playlist != 0;
}

/* Count playable rows, load the pause directives, and retain a nonzero weight
   total only when every playable row carries a parenthesized weight. */
// FUNCTION: WIZ8 0x0048FF50
int AnalyzeMusicPlaylist(stScript* playlist, int* total_weight)
{
    int playable_count = 0;
    bool found_unweighted = false;

    *total_weight = 0;
    for (const auto& entry : playlist->lines) {
        const char* line = entry.text.c_str();

        if (line[0] == '#') {
            if (wiz8::iequals_prefix(line + 1, "PAUSEMIN=", 9)) {
                g_music_pause_min_seconds = atoi(line + 10);
            } else if (wiz8::iequals_prefix(line + 1, "PAUSEMAX=", 9)) {
                g_music_pause_max_seconds = atoi(line + 10);
            } else if (wiz8::iequals_prefix(line + 1, "PAUSECHANCE=", 12)) {
                g_music_pause_chance_percent = atoi(line + 13);
            }
            continue;
        }

        ++playable_count;
        if (!found_unweighted) {
            const char* weight = strchr(line, '(');
            if (weight != 0) {
                *total_weight += atoi(weight + 1);
            } else {
                found_unweighted = true;
                *total_weight = 0;
            }
        }
    }
    return playable_count;
}

// FUNCTION: WIZ8 0x0048F9E0
void ServiceMusicPlaylist(void)
{
    int failures = 0;

    if (!g_music_playlist_active) {
        return;
    }
    if (g_music_sample_handle != nullptr && wiz8::sound::is_playing(g_music_sample_handle)) {
        return;
    }
    if (w8_get_ticks() <= g_music_playlist_tick) {
        return;
    }

    if (!g_music_force_next && !gXStatus.fCombatMode &&
        Random(100) <= static_cast<unsigned int>(g_music_pause_chance_percent)) {
        g_music_playlist_tick =
            w8_get_ticks() + g_music_pause_min_seconds * 1000 +
            Random(g_music_pause_max_seconds * 1000 - g_music_pause_min_seconds * 1000);
        return;
    }

    for (;;) {
        int selected = -1;

        if (g_music_playlist_weight_total == 0) {
            selected = Random(g_music_playlist_track_count);
        } else {
            unsigned int target = Random(g_music_playlist_weight_total);
            unsigned int accumulated = 0;
            for (int index = 0; index < static_cast<int>(g_music_playlist->lines.size()); ++index) {
                const char* line = g_music_playlist->lines[index].text.c_str();
                if (line[0] == '#') {
                    continue;
                }
                selected = index;
                accumulated += atoi(strchr(line, '(') + 1);
                if (target < accumulated) {
                    break;
                }
            }
        }

        if (selected < 0) {
            g_music_force_next = false;
            return;
        }

        const auto& line = g_music_playlist->lines[selected].text;
        const auto track = line.substr(0, line.find('('));

        if (StartMusicResource(track.c_str(), g_music_fade, 1) == 0) {
            ++failures;
        }
        if (failures > 4 || g_music_sample_handle != nullptr) {
            g_music_force_next = false;
            return;
        }
    }
}

// FUNCTION: WIZ8 0x0048FC10
unsigned char StartMusicResource(const char* resource, int fade, unsigned char replace_current)
{
    std::string path;

    if (resource == 0) {
        return 0;
    }
    if (g_dev_mode) {
        RequestExitScreen();
    }

    path = fmt::format("Data\\Music\\{}", resource);
    wiz8::uppercase_ascii(path.data());
    g_music_fade = static_cast<unsigned char>(fade);

    if (strstr(path.c_str(), ".MPL") == 0) {
        if (!g_music_playlist_active) {
            g_music_playlist->setName("");
        }

        const bool crossfade = g_music_fade != 0;
        auto handle = wiz8::sound::stream(path, {.volume = std::uint32_t(g_settings.music_volume),
                                                 .group = wiz8::sound::Group::music,
                                                 .fade_in_ms = crossfade ? 5000u : 0u});
        if (handle == nullptr) {
            return 0;
        }
        if (g_music_sample_handle != nullptr) {
            wiz8::sound::stop(g_music_sample_handle, crossfade ? 2000u : 0u);
        }
        g_music_sample_handle = handle;
        return 1;
    }

    if (wiz8::iequals(resource, g_music_playlist->getName().c_str())) {
        return 1;
    }

    g_music_playlist_tick = w8_get_ticks() - 1;
    g_music_pause_min_seconds = 0;
    g_music_pause_max_seconds = 0;
    g_music_pause_chance_percent = 0;
    g_music_playlist->Clear();
    g_music_playlist->Load(path.c_str());

    if (g_music_playlist->lines.empty()) {
        return 0;
    }
    if (replace_current != 0) {
        if (g_music_sample_handle != nullptr) {
            if (g_music_fade == 0) {
                wiz8::sound::stop(g_music_sample_handle);
            } else {
                wiz8::sound::stop(g_music_sample_handle, 2000);
            }
        }
        g_music_sample_handle = nullptr;
    }

    g_music_playlist_track_count =
        AnalyzeMusicPlaylist(g_music_playlist, &g_music_playlist_weight_total);
    if (g_music_playlist_track_count != 0) {
        g_music_playlist->setName(resource);
        g_music_playlist_active = true;
        g_music_force_next = true;
    }
    return 1;
}

// FUNCTION: WIZ8 0x0048FF00
void StopMusicPlaylist(bool fade)
{
    if (fade) {
        if (g_music_sample_handle != nullptr) {
            wiz8::sound::stop(g_music_sample_handle, 2000);
            g_music_sample_handle = nullptr;
            g_music_playlist_active = false;
            return;
        }
    } else {
        wiz8::sound::stop_group(wiz8::sound::Group::music);
    }
    g_music_sample_handle = nullptr;
    g_music_playlist_active = false;
}
