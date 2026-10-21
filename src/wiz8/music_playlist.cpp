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
#include "local_screens/Screens.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/entities/script.h"
#include "wiz8/game.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/platform/random.h"
#include "wiz8/platform/timer.h"

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

wiz8::sound::Playback g_music_sample_handle = nullptr;
static Script* g_music_playlist;
static uint64_t g_music_playlist_tick;
bool g_music_playlist_active;
// GLOBAL: WIZ8 0x0060AAE4
static unsigned char g_music_fade = 1;
static bool g_music_force_next = true;

namespace {
struct MusicPlaylistInfo {
    int track_count = 0;
    int weight_total = 0;
    int pause_min_seconds = 0;
    int pause_max_seconds = 0;
    int pause_chance_percent = 0;
};
MusicPlaylistInfo playlist_info;
}

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

bool InitializeMusicPlaylist(void)
{
    g_music_playlist = new Script();
    if (g_music_playlist) {
        g_music_playlist->setName("Music Playlist");
    }
    g_music_playlist_tick = MonotonicMilliseconds();
    playlist_info.pause_min_seconds = 0;
    playlist_info.pause_max_seconds = 0;
    playlist_info.pause_chance_percent = 0;
    return g_music_playlist != 0;
}

/* Count playable rows, load the pause directives, and retain a nonzero weight
   total only when every playable row carries a parenthesized weight. */
// FUNCTION: WIZ8 0x0048FF50
static MusicPlaylistInfo AnalyzeMusicPlaylist(const Script& playlist)
{
    MusicPlaylistInfo parsed;
    bool found_unweighted = false;

    for (const auto& entry : playlist.lines) {
        const char* line = entry.text.c_str();

        if (line[0] == '#') {
            if (SDL_strncasecmp(line + 1, "PAUSEMIN=", 9) == 0) {
                parsed.pause_min_seconds = atoi(line + 10);
            } else if (SDL_strncasecmp(line + 1, "PAUSEMAX=", 9) == 0) {
                parsed.pause_max_seconds = atoi(line + 10);
            } else if (SDL_strncasecmp(line + 1, "PAUSECHANCE=", 12) == 0) {
                parsed.pause_chance_percent = atoi(line + 13);
            }
            continue;
        }

        ++parsed.track_count;
        if (!found_unweighted) {
            const char* weight = strchr(line, '(');
            if (weight != 0) {
                parsed.weight_total += atoi(weight + 1);
            } else {
                found_unweighted = true;
                parsed.weight_total = 0;
            }
        }
    }
    return parsed;
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
    if (MonotonicMilliseconds() <= g_music_playlist_tick) {
        return;
    }

    if (!g_music_force_next && !CurrentGame().combat.has_value() &&
        PresentationRng().below(100) <=
            static_cast<unsigned int>(playlist_info.pause_chance_percent)) {
        g_music_playlist_tick = MonotonicMilliseconds() + playlist_info.pause_min_seconds * 1000 +
                                PresentationRng().below(playlist_info.pause_max_seconds * 1000 -
                                                        playlist_info.pause_min_seconds * 1000);
        return;
    }

    for (;;) {
        int selected = -1;

        if (playlist_info.weight_total == 0) {
            selected = PresentationRng().below(playlist_info.track_count);
        } else {
            unsigned int target = PresentationRng().below(playlist_info.weight_total);
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

bool StartMusicResource(const char* resource, int fade, unsigned char replace_current)
{
    std::string path;

    if (resource == 0) {
        return false;
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
            return false;
        }
        if (g_music_sample_handle != nullptr) {
            wiz8::sound::stop(g_music_sample_handle, crossfade ? 2000u : 0u);
        }
        g_music_sample_handle = handle;
        return true;
    }

    if (SDL_strcasecmp(resource, g_music_playlist->getName().c_str()) == 0) {
        return true;
    }

    g_music_playlist_tick = MonotonicMilliseconds() - 1;
    playlist_info.pause_min_seconds = 0;
    playlist_info.pause_max_seconds = 0;
    playlist_info.pause_chance_percent = 0;
    g_music_playlist->Clear();
    g_music_playlist->Load(path.c_str());

    if (g_music_playlist->lines.empty()) {
        return false;
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

    playlist_info = AnalyzeMusicPlaylist(*g_music_playlist);
    if (playlist_info.track_count != 0) {
        g_music_playlist->setName(resource);
        g_music_playlist_active = true;
        g_music_force_next = true;
    }
    return true;
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
