#ifndef WIZ8_MUSIC_PLAYLIST_H
#define WIZ8_MUSIC_PLAYLIST_H

#include "wiz8/audio_resources.h"

extern wiz8::sound::Playback g_music_sample_handle;
extern bool g_music_playlist_active;

unsigned char InitializeMusicPlaylist(void);
unsigned char StartMusicResource(const char* resource, int fade, unsigned char replace_current);

void ServiceMusicPlaylist(void);
void StopMusicPlaylist(bool fade);

bool IsCurrentMusicPlaylist(const char* playlist);

#endif
