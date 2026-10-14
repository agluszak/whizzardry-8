#include <climits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_properties.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <wiz8/filesystem.h>
#include <wiz8/native_audio.h>

#include "native/audio_test.h"
#include "native/movie_audio.h"

namespace {
std::shared_ptr<MIX_Mixer> mixer;
std::weak_ptr<MIX_Mixer> retained_mixer;
bool offline = false;

} // namespace

namespace w8_native {
bool initialize_audio()
{
    if (mixer) return true;
    mixer = retained_mixer.lock();
    if (mixer) return true;
    if (!MIX_Init()) return false;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 44100};
    auto* created = offline ? MIX_CreateMixer(&spec)
                            : MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!created) {
        MIX_Quit();
        return false;
    }
    mixer = std::shared_ptr<MIX_Mixer>(created, [](MIX_Mixer* value) {
        MIX_DestroyMixer(value);
        MIX_Quit();
    });
    retained_mixer = mixer;
    return true;
}
void shutdown_audio()
{
    mixer.reset();
}
MIX_Mixer* audio_mixer()
{
    return mixer.get();
}
std::shared_ptr<MIX_Mixer> retain_audio_mixer()
{
    return mixer;
}

AudioInput open_audio_input(const char* path)
{
    if (!path) {
        SDL_SetError("Null audio path");
        return {};
    }
    try {
        auto file = wiz8::open_file(path);
        if (file->size() <= 0) {
            SDL_SetError("Empty audio stream: %s", path);
            return {};
        }
        // Loose files and SLF entries already own independent, seekable streams.
        return AudioInput(file->release_io());
    } catch (const std::exception& error) {
        SDL_SetError("Audio input: %s", error.what());
        return {};
    }
}
void audio_offline_for_test(bool enabled)
{
    if (!retained_mixer.expired())
        throw std::runtime_error("Cannot change audio mode while initialized");
    offline = enabled;
}
bool audio_render_for_test(float* samples, size_t frames)
{
    return mixer && offline && samples && frames <= size_t(INT_MAX) / (2 * sizeof(float)) &&
           MIX_Generate(mixer.get(), samples, int(frames * 2 * sizeof(float))) >= 0;
}

struct MovieAudio::State {
    std::shared_ptr<MIX_Mixer> owner = retain_audio_mixer();
    PCMStream stream;
    Track track;
    unsigned rate, channels;
    bool finished = false;
    State(unsigned frequency, unsigned channel_count) : rate(frequency), channels(channel_count)
    {
        if (!owner || !rate || rate > INT_MAX || !channels || channels > 8)
            throw std::runtime_error("Movie audio has no valid mixer/PCM format");
        SDL_AudioSpec input{SDL_AUDIO_F32, int(channels), int(rate)}, output{};
        if (!MIX_GetMixerFormat(owner.get(), &output)) throw std::runtime_error(SDL_GetError());
        stream.reset(SDL_CreateAudioStream(&input, &output));
        track.reset(MIX_CreateTrack(owner.get()));
        if (!stream || !track || !MIX_SetTrackAudioStream(track.get(), stream.get()))
            throw std::runtime_error(SDL_GetError());
    }
};
MovieAudio::MovieAudio(unsigned rate, unsigned channels)
    : state(std::make_unique<State>(rate, channels))
{
}
MovieAudio::~MovieAudio() = default;
void MovieAudio::append(const float* samples, size_t frames)
{
    const auto queued = SDL_GetAudioStreamQueued(state->stream.get());
    if (state->finished || queued < 0 || !samples || frames > state->rate * 2 ||
        uint64_t(queued) / (state->channels * sizeof(float)) + frames > state->rate * 2)
        throw std::runtime_error("Movie audio exceeded two seconds of lookahead or finished");
    if (!SDL_PutAudioStreamData(state->stream.get(), samples,
                                int(frames * state->channels * sizeof(float))))
        throw std::runtime_error(SDL_GetError());
}
void MovieAudio::start()
{
    const auto options = SDL_CreateProperties();
    if (!options) throw std::runtime_error(SDL_GetError());
    const bool ok =
        SDL_SetBooleanProperty(options, MIX_PROP_PLAY_HALT_WHEN_EXHAUSTED_BOOLEAN, false) &&
        MIX_PlayTrack(state->track.get(), options);
    SDL_DestroyProperties(options);
    if (!ok) throw std::runtime_error(SDL_GetError());
}
void MovieAudio::finish()
{
    if (!SDL_FlushAudioStream(state->stream.get())) throw std::runtime_error(SDL_GetError());
    state->finished = true;
}
bool MovieAudio::drained() const
{
    return state->finished && SDL_GetAudioStreamAvailable(state->stream.get()) == 0;
}
bool movie_audio_available()
{
    return mixer != nullptr;
}
} // namespace w8_native
