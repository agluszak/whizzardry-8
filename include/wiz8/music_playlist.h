#ifndef WIZ8_MUSIC_PLAYLIST_H
#define WIZ8_MUSIC_PLAYLIST_H

#include <string>
#include <string_view>
#include <vector>

#include "wiz8/audio_resources.h"

struct MusicPlaylist {
    struct Track {
        std::string resource;
        int weight;
    };
    std::string resource;
    std::vector<Track> tracks;
    int weight_total = 0;
    int pause_min_seconds = 0;
    int pause_max_seconds = 0;
    int pause_chance_percent = 0;

    static MusicPlaylist Parse(std::string resource, std::string_view text);
    const Track& Select(unsigned int draw) const;
};

extern wiz8::sound::Playback g_music_sample_handle;
extern bool g_music_playlist_active;

bool InitializeMusicPlaylist(void);
bool StartMusicResource(const char* resource, int fade, unsigned char replace_current);

void ServiceMusicPlaylist(void);
void StopMusicPlaylist(bool fade);

bool IsCurrentMusicPlaylist(const char* playlist);

#endif
