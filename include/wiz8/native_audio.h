#pragma once

#include <SDL3_mixer/SDL_mixer.h>
#include <memory>

namespace w8_native
{
struct AudioDeleter
{
    void operator()(MIX_Audio* audio) const { MIX_DestroyAudio(audio); }
};
struct TrackDeleter
{
    void operator()(MIX_Track* track) const { MIX_DestroyTrack(track); }
};
struct AudioInputDeleter
{
    void operator()(SDL_IOStream* stream) const { SDL_CloseIO(stream); }
};
struct PCMDeleter
{
    void operator()(SDL_AudioStream* stream) const { SDL_DestroyAudioStream(stream); }
};
using Audio = std::unique_ptr<MIX_Audio, AudioDeleter>;
using Track = std::unique_ptr<MIX_Track, TrackDeleter>;
using AudioInput = std::unique_ptr<SDL_IOStream, AudioInputDeleter>;
using PCMStream = std::unique_ptr<SDL_AudioStream, PCMDeleter>;

bool initialize_audio();
void shutdown_audio();
MIX_Mixer* audio_mixer();
// Movie tracks retain the mixer if soundman shuts down before the movie.
std::shared_ptr<MIX_Mixer> retain_audio_mixer();
// An independent, seekable stream bounded to its loose file or SLF entry.
AudioInput open_audio_input(const char* game_path);
} // namespace w8_native
