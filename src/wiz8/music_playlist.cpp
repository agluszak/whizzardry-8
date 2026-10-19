#include "wiz8/music_playlist.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>

#include "audio_resources.h"
#include "compat/native.h"
#include "local_screens/Screens.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/engine_code/stScript.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/platform/random.h"
#include "wiz8/ui_input.h"
#include "wiz8/xstatus.h"

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

wiz8::sound::Playback g_music_sample_handle = nullptr;
static stScript* g_music_playlist;
static uint64_t g_music_playlist_tick;
bool g_music_playlist_active;
int g_music_playlist_weight_total;
int g_music_playlist_track_count;
static unsigned char g_music_fade = 1;
static bool g_music_force_next = true;
int g_music_pause_min_seconds = 20;
int g_music_pause_max_seconds = 60;
int g_music_pause_chance_percent = 30;

void SetMusicVolume(unsigned char volume)
{
    g_settings.music_volume = volume;
    if (g_music_sample_handle != nullptr) {
        wiz8::sound::set_volume(g_music_sample_handle, volume);
    }
}

bool IsMusicMuted(void)
{
    return g_settings.muted_music_volume != 0xff;
}

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

bool IsCurrentMusicPlaylist(const char* playlist)
{
    return SDL_strcasecmp(playlist, g_music_playlist->getName().c_str()) == 0;
}

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
int AnalyzeMusicPlaylist(stScript* playlist, int* total_weight)
{
    int playable_count = 0;
    bool found_unweighted = false;

    *total_weight = 0;
    for (const auto& entry : playlist->lines) {
        const char* line = entry.text.c_str();

        if (line[0] == '#') {
            if (SDL_strncasecmp(line + 1, "PAUSEMIN=", 9) == 0) {
                g_music_pause_min_seconds = atoi(line + 10);
            } else if (SDL_strncasecmp(line + 1, "PAUSEMAX=", 9) == 0) {
                g_music_pause_max_seconds = atoi(line + 10);
            } else if (SDL_strncasecmp(line + 1, "PAUSECHANCE=", 12) == 0) {
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
    SDL_strupr(path.data());
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

    if (SDL_strcasecmp(resource, g_music_playlist->getName().c_str()) == 0) {
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
