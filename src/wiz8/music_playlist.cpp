#include "wiz8/music_playlist.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>

#include "audio_resources.h"
#include "local_screens/Screens.h"
#include "wiz8/asset_paths.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/platform/random.h"
#include "wiz8/platform/timer.h"
#include "wiz8/ui_input.h"

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

wiz8::sound::Playback g_music_sample_handle = nullptr;
static MusicPlaylist g_music_playlist;
static uint64_t g_music_playlist_tick;
bool g_music_playlist_active;
// GLOBAL: WIZ8 0x0060AAE4
static unsigned char g_music_fade = 1;
static bool g_music_force_next = true;

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
    return SDL_strcasecmp(playlist, g_music_playlist.resource.c_str()) == 0;
}

bool InitializeMusicPlaylist(void)
{
    g_music_playlist = {};
    g_music_playlist_tick = MonotonicMilliseconds();
    return true;
}

/* Count playable rows, load the pause directives, and retain a nonzero weight
   total only when every playable row carries a parenthesized weight. */
// FUNCTION: WIZ8 0x0048FF50
MusicPlaylist MusicPlaylist::Parse(std::string resource, std::string_view text)
{
    MusicPlaylist parsed;
    parsed.resource = std::move(resource);
    bool found_unweighted = false;
    std::istringstream input{std::string(text)};
    std::string row;
    while (std::getline(input, row)) {
        if (row.ends_with('\r')) row.pop_back();
        // Preserve the text script loader's blank/comment/label handling.
        const auto start = row.find_first_not_of(" \t");
        if (start == row.npos || row[start] == '*') continue;
        const auto stop = row.find_first_of(" \t", start);
        const auto token =
            std::string_view(row).substr(start, stop == row.npos ? stop : stop - start);
        if (token.ends_with(':')) continue;
        const char* line = row.c_str();
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
        const auto weight_start = row.find('(');
        const int weight = weight_start == row.npos ? 0 : atoi(line + weight_start + 1);
        found_unweighted |= weight_start == row.npos;
        parsed.tracks.push_back({row.substr(0, weight_start), weight});
        if (!found_unweighted) parsed.weight_total += weight;
    }
    if (found_unweighted) parsed.weight_total = 0;
    return parsed;
}

const MusicPlaylist::Track& MusicPlaylist::Select(unsigned int draw) const
{
    if (weight_total == 0) return tracks[draw];
    unsigned int accumulated = 0;
    for (const auto& track : tracks) {
        accumulated += track.weight;
        if (draw < accumulated) return track;
    }
    return tracks.back();
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
            static_cast<unsigned int>(g_music_playlist.pause_chance_percent)) {
        g_music_playlist_tick = MonotonicMilliseconds() +
                                g_music_playlist.pause_min_seconds * 1000 +
                                PresentationRng().below(g_music_playlist.pause_max_seconds * 1000 -
                                                        g_music_playlist.pause_min_seconds * 1000);
        return;
    }

    for (;;) {
        const auto draw = PresentationRng().below(g_music_playlist.weight_total == 0
                                                      ? g_music_playlist.tracks.size()
                                                      : g_music_playlist.weight_total);
        const auto& track = g_music_playlist.Select(draw).resource;

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
    if (g_dev_mode) {
        RequestExitScreen();
    }

    path = fmt::format("Data\\Music\\{}", resource);
    SDL_strupr(path.data());
    if (strstr(path.c_str(), ".MPL") == 0) {
        g_music_fade = static_cast<unsigned char>(fade);
        if (!g_music_playlist_active) {
            g_music_playlist.resource.clear();
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

    if (SDL_strcasecmp(resource, g_music_playlist.resource.c_str()) == 0) {
        return true;
    }

    MusicPlaylist candidate;
    try {
        candidate =
            MusicPlaylist::Parse(resource, wiz8::open_file(wiz8::asset_path(path))->read_text());
    } catch (const std::exception&) {
        return false;
    }
    if (candidate.tracks.empty()) return false;
    g_music_fade = static_cast<unsigned char>(fade);
    g_music_playlist = std::move(candidate);
    g_music_playlist_tick = MonotonicMilliseconds() - 1;
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

    g_music_playlist_active = true;
    g_music_force_next = true;
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
