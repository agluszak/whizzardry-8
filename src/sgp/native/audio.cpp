#include <wiz8/native_audio.h>
#include <wiz8/filesystem.h>
#include "FileMan.h"
#include "LibraryDataBase.h"
#include "native/audio_test.h"
#include "native/movie_audio.h"
#include <algorithm>
#include <climits>
#include <stdexcept>

namespace
{
std::shared_ptr<MIX_Mixer> mixer;
std::weak_ptr<MIX_Mixer> retained_mixer;
bool offline = false;

struct Input
{
    std::unique_ptr<wiz8::File> file;
    Sint64 start = 0, length = 0, position = 0;

    static Sint64 SDLCALL size(void* data) { return static_cast<Input*>(data)->length; }
    static Sint64 SDLCALL seek(void* data, Sint64 offset, SDL_IOWhence whence)
    {
        auto& input = *static_cast<Input*>(data);
        const Sint64 base = whence == SDL_IO_SEEK_SET ? 0 :
                            whence == SDL_IO_SEEK_CUR ? input.position :
                            whence == SDL_IO_SEEK_END ? input.length : -1;
        if (base < 0 || offset < -base || offset > input.length - base)
        {
            SDL_SetError("Audio seek outside input extent");
            return -1;
        }
        try
        {
            input.position = input.file->seek(input.start + base + offset,
                                               wiz8::SeekOrigin::begin) - input.start;
            return input.position;
        }
        catch (const std::exception& error) { SDL_SetError("%s", error.what()); return -1; }
    }
    static size_t SDLCALL read(void* data, void* bytes, size_t count, SDL_IOStatus* status)
    {
        auto& input = *static_cast<Input*>(data);
        const auto request = std::min<uint64_t>(count, input.length - input.position);
        try
        {
            const auto result = input.file->read(bytes, request);
            input.position += result.bytes;
            *status = result.bytes == count ? SDL_IO_STATUS_READY :
                      input.position == input.length ? SDL_IO_STATUS_EOF : SDL_IO_STATUS_ERROR;
            if (*status == SDL_IO_STATUS_ERROR) SDL_SetError("Truncated audio input");
            return result.bytes;
        }
        catch (const std::exception& error)
        {
            SDL_SetError("%s", error.what());
            *status = SDL_IO_STATUS_ERROR;
            return 0;
        }
    }
    static bool SDLCALL close(void* data) { delete static_cast<Input*>(data); return true; }
};
} // namespace

namespace w8_native
{
bool initialize_audio()
{
    if (mixer) return true;
    mixer = retained_mixer.lock();
    if (mixer) return true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
    if (!MIX_Init()) { SDL_QuitSubSystem(SDL_INIT_AUDIO); return false; }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 44100};
    auto* created = offline ? MIX_CreateMixer(&spec) :
                              MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!created)
    {
        MIX_Quit();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    mixer = std::shared_ptr<MIX_Mixer>(created, [](MIX_Mixer* value) {
        MIX_DestroyMixer(value);
        MIX_Quit();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    });
    retained_mixer = mixer;
    return true;
}
void shutdown_audio() { mixer.reset(); }
MIX_Mixer* audio_mixer() { return mixer.get(); }
std::shared_ptr<MIX_Mixer> retain_audio_mixer() { return mixer; }

AudioInput open_audio_input(const char* path)
{
    if (!path) { SDL_SetError("Null audio path"); return {}; }
    try
    {
        auto input = std::make_unique<Input>();
        if (FileExistsNoDB(const_cast<char*>(path)))
        {
            input->file = wiz8::open_file(path);
            input->length = input->file->size();
        }
        else
        {
            const auto handle = FileOpen(const_cast<char*>(path), FILE_ACCESS_READ | FILE_OPEN_EXISTING, FALSE);
            if (!handle) { SDL_SetError("Cannot open audio input: %s", path); return {}; }
            input->length = FileGetSize(handle);
            input->file.reset(OpenLibraryStream(handle));
            FileClose(handle);
        }
        if (!input->file) { SDL_SetError("Cannot open audio stream: %s", path); return {}; }
        input->start = input->file->tell();
        const auto size = input->file->size();
        if (input->start < 0 || input->length <= 0 || input->start > size ||
            input->length > size - input->start)
        {
            SDL_SetError("Audio input exceeds its file");
            return {};
        }
        SDL_IOStreamInterface interface{};
        SDL_INIT_INTERFACE(&interface);
        interface.size = Input::size;
        interface.seek = Input::seek;
        interface.read = Input::read;
        interface.close = Input::close;
        AudioInput stream(SDL_OpenIO(&interface, input.get()));
        if (stream) (void)input.release();
        return stream;
    }
    catch (const std::exception& error) { SDL_SetError("Audio input: %s", error.what()); return {}; }
}
void audio_offline_for_test(bool enabled)
{
    if (!retained_mixer.expired()) throw std::runtime_error("Cannot change audio mode while initialized");
    offline = enabled;
}
bool audio_render_for_test(float* samples, size_t frames)
{
    return mixer && offline && samples && frames <= size_t(INT_MAX) / (2 * sizeof(float)) &&
           MIX_Generate(mixer.get(), samples, int(frames * 2 * sizeof(float))) >= 0;
}

struct MovieAudio::State
{
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
MovieAudio::MovieAudio(unsigned rate, unsigned channels) : state(std::make_unique<State>(rate, channels)) {}
MovieAudio::~MovieAudio() = default;
void MovieAudio::append(const float* samples, size_t frames)
{
    const auto queued = SDL_GetAudioStreamQueued(state->stream.get());
    if (state->finished || queued < 0 || !samples || frames > state->rate * 2 ||
        uint64_t(queued) / (state->channels * sizeof(float)) + frames > state->rate * 2)
        throw std::runtime_error("Movie audio exceeded two seconds of lookahead or finished");
    if (!SDL_PutAudioStreamData(state->stream.get(), samples, int(frames * state->channels * sizeof(float))))
        throw std::runtime_error(SDL_GetError());
}
void MovieAudio::start()
{
    const auto options = SDL_CreateProperties();
    if (!options) throw std::runtime_error(SDL_GetError());
    const bool ok = SDL_SetBooleanProperty(options, MIX_PROP_PLAY_HALT_WHEN_EXHAUSTED_BOOLEAN, false) &&
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
bool movie_audio_available() { return mixer != nullptr; }
} // namespace w8_native
