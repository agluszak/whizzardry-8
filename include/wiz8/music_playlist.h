#ifndef WIZ8_MUSIC_PLAYLIST_H
#define WIZ8_MUSIC_PLAYLIST_H

#include <cstdint>
#include "wiz8/audio_resources.h"

extern wiz8::sound::Playback g_music_sample_handle;
extern bool g_music_playlist_active;
extern std::int32_t g_music_playlist_weight_total;
extern std::int32_t g_music_playlist_track_count;
extern std::int32_t g_music_pause_min_seconds;
extern std::int32_t g_music_pause_max_seconds;
extern std::int32_t g_music_pause_chance_percent;

std::uint8_t InitializeMusicPlaylist(void);
std::uint8_t StartMusicResource(const char* resource, std::int32_t fade, std::uint8_t replace_current);

void ServiceMusicPlaylist(void);
void StopMusicPlaylist(bool fade);

bool IsCurrentMusicPlaylist(const char* playlist);

#endif
