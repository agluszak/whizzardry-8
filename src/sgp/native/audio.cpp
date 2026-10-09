#include "compat/audio.h"
#include "FileMan.h"
#include "LibraryDataBase.h"
#include "compat/platform.h"
#include "miniaudio.h"
#include "native/audio_test.h"
#include "native/movie_audio.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace
{
struct Source
{
    ma_data_source_base base{};
    ma_decoder decoder{};
    HANDLE file = INVALID_HANDLE_VALUE;
    uint64_t start = 0, length = 0, position = 0;
    ma_uint32 channels = 0, rate = 0;
    ma_uint64 frames = 0;
    std::mutex decoder_mutex;
    std::vector<unsigned char> encoded;
    std::atomic<UINT32> loops{1};
    bool initialized = false;
};
struct Voice
{
    Source source;
    HDIGDRIVER driver = nullptr;
    ma_sound sound{};
    int rate = 0, volume = 127;
    bool initialized = false, listener = false;
    ~Voice();
};
HDIGDRIVER active_driver = nullptr;
bool offline = false;
char last_error[160]{};
int mixer_channels = 32, waveout = 0;
ma_result record(ma_result result)
{
    if (result != MA_SUCCESS)
        snprintf(last_error, sizeof(last_error), "miniaudio: %s", ma_result_description(result));
    return result;
}
ma_result read_pcm(ma_data_source* data, void* output, ma_uint64 frames, ma_uint64* read)
{
    auto& source = *reinterpret_cast<Source*>(data);
    std::lock_guard<std::mutex> lock(source.decoder_mutex);
    ma_uint64 done = 0;
    while (done < frames)
    {
        ma_uint64 count = 0;
        auto result = ma_decoder_read_pcm_frames(
            &source.decoder,
            output ? static_cast<float*>(output) + done * source.channels : nullptr, frames - done,
            &count);
        done += count;
        if (done == frames)
            break;
        if (result != MA_SUCCESS && result != MA_AT_END)
        {
            *read = done;
            return result;
        }
        UINT32 loops = source.loops.load();
        if (loops == 1)
            break;
        if (!source.frames)
            break;
        if (loops)
            --source.loops;
        if (ma_decoder_seek_to_pcm_frame(&source.decoder, 0) != MA_SUCCESS)
            break;
    }
    *read = done;
    return done == frames ? MA_SUCCESS : MA_AT_END;
}
ma_result seek_pcm(ma_data_source* data, ma_uint64 frame)
{
    auto& source = *reinterpret_cast<Source*>(data);
    std::lock_guard<std::mutex> lock(source.decoder_mutex);
    return ma_decoder_seek_to_pcm_frame(&source.decoder, frame);
}
ma_result format_pcm(ma_data_source* data, ma_format* format, ma_uint32* channels, ma_uint32* rate,
                     ma_channel* map, size_t capacity)
{
    return ma_decoder_get_data_format(&reinterpret_cast<Source*>(data)->decoder, format, channels,
                                      rate, map, capacity);
}
ma_result cursor_pcm(ma_data_source* data, ma_uint64* cursor)
{
    auto& source = *reinterpret_cast<Source*>(data);
    std::lock_guard<std::mutex> lock(source.decoder_mutex);
    return ma_decoder_get_cursor_in_pcm_frames(&source.decoder, cursor);
}
ma_result length_pcm(ma_data_source* data, ma_uint64* length)
{
    *length = reinterpret_cast<Source*>(data)->frames;
    return MA_SUCCESS;
}
const ma_data_source_vtable source_table = {read_pcm,   seek_pcm, format_pcm, cursor_pcm,
                                            length_pcm, nullptr,  0};
ma_result read_file(ma_decoder* decoder, void* output, size_t size, size_t* read)
{
    auto& source = *static_cast<Source*>(decoder->pUserData);
    DWORD count = 0;
    DWORD request =
        std::min<uint64_t>(std::min<uint64_t>(size, UINT32_MAX), source.length - source.position);
    if (!W8ReadFile(source.file, output, request, &count, nullptr))
        return MA_IO_ERROR;
    source.position += count;
    *read = count;
    return count ? MA_SUCCESS : MA_AT_END;
}
ma_result seek_file(ma_decoder* decoder, ma_int64 offset, ma_seek_origin origin)
{
    auto& source = *static_cast<Source*>(decoder->pUserData);
    int64_t base = origin == ma_seek_origin_start     ? 0
                   : origin == ma_seek_origin_current ? source.position
                                                      : source.length;
    if (offset < -base || offset > int64_t(source.length) - base)
        return MA_BAD_SEEK;
    uint64_t position = base + offset;
    uint64_t absolute = source.start + position;
    LONG high = LONG(absolute >> 32);
    DWORD low = W8SetFilePointer(source.file, LONG(absolute), &high, FILE_BEGIN);
    if (low == INVALID_SET_FILE_POINTER && W8GetLastError())
        return MA_BAD_SEEK;
    source.position = position;
    return MA_SUCCESS;
}
void clear(Voice& voice)
{
    if (voice.initialized)
    {
        ma_sound_uninit(&voice.sound);
        voice.initialized = false;
    }
    if (voice.source.initialized)
    {
        ma_decoder_uninit(&voice.source.decoder);
        ma_data_source_uninit(&voice.source.base);
        voice.source.initialized = false;
    }
    voice.source.encoded.clear();
    if (voice.source.file != INVALID_HANDLE_VALUE)
    {
        W8CloseHandle(voice.source.file);
        voice.source.file = INVALID_HANDLE_VALUE;
    }
}
} // namespace
struct _SAMPLE : Voice
{
};
struct _STREAM : Voice
{
};
struct h3DPOBJECT : Voice
{
};
struct _DIG_DRIVER
{
    ma_engine engine{};
    h3DPOBJECT listener;
};
namespace
{
Voice::~Voice()
{
    clear(*this);
}
bool attach(Voice& voice, bool spatial, ma_result result)
{
    if (record(result) != MA_SUCCESS)
        return false;
    voice.source.initialized = true;
    auto config = ma_data_source_config_init();
    config.vtable = &source_table;
    if (record(ma_data_source_init(&config, &voice.source.base)) != MA_SUCCESS)
        return false;
    if (record(ma_decoder_get_data_format(&voice.source.decoder, nullptr, &voice.source.channels,
                                          &voice.source.rate, nullptr, 0)) != MA_SUCCESS)
        return false;
    if (record(ma_decoder_get_length_in_pcm_frames(&voice.source.decoder, &voice.source.frames)) !=
        MA_SUCCESS)
        return false;
    if (!voice.source.rate || !voice.source.frames)
        return false;
    if (record(ma_sound_init_from_data_source(&voice.driver->engine, &voice.source.base,
                                              spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION,
                                              nullptr, &voice.sound)) != MA_SUCCESS)
        return false;
    voice.initialized = true;
    voice.rate = voice.source.rate;
    ma_sound_set_volume(&voice.sound, voice.volume / 127.f);
    return true;
}
bool memory(Voice* voice, const void* bytes, size_t size, bool spatial)
{
    if (!voice || !voice->driver || !bytes || !size)
        return false;
    clear(*voice);
    voice->source.encoded.assign(static_cast<const unsigned char*>(bytes),
                                 static_cast<const unsigned char*>(bytes) + size);
    auto config = ma_decoder_config_init(ma_format_f32, 0, 0);
    bool ok = attach(*voice, spatial,
                     ma_decoder_init_memory(voice->source.encoded.data(), size, &config,
                                            &voice->source.decoder));
    if (!ok)
        clear(*voice);
    return ok;
}
void volume(Voice* voice, int value)
{
    if (!voice)
        return;
    voice->volume = std::clamp(value, 0, 127);
    if (voice->initialized)
        ma_sound_set_volume(&voice->sound, voice->volume / 127.f);
}
void rate(Voice* voice, int value)
{
    if (!voice || !voice->initialized || value <= 0)
        return;
    voice->rate = value;
    ma_sound_set_pitch(&voice->sound, float(value) / voice->source.rate);
}
void pan(Voice* voice, int value)
{
    if (voice && voice->initialized)
    {
        value = std::clamp(value, 0, 127);
        ma_sound_set_pan(&voice->sound, value <= 64 ? (value - 64) / 64.f : (value - 64) / 63.f);
    }
}
void loop(Voice* voice, UINT32 count)
{
    if (voice)
        voice->source.loops = count;
}
void start(Voice* voice)
{
    if (voice && voice->initialized)
        record(ma_sound_start(&voice->sound));
}
void stop(Voice* voice)
{
    if (voice && voice->initialized)
        record(ma_sound_stop(&voice->sound));
}
UINT32 status(Voice* voice)
{
    return voice && voice->initialized && ma_sound_is_playing(&voice->sound) &&
                   !ma_sound_at_end(&voice->sound)
               ? SMP_PLAYING
               : SMP_DONE;
}
void position(Voice* voice, S32* total, S32* current)
{
    ma_uint64 length = 0, cursor = 0;
    if (voice && voice->initialized)
    {
        length = voice->source.frames;
        ma_sound_get_cursor_in_pcm_frames(&voice->sound, &cursor);
        if (total)
            *total = S32(std::min<ma_uint64>(length * 1000 / voice->source.rate, INT32_MAX));
        if (current)
            *current = S32(std::min<ma_uint64>(cursor * 1000 / voice->source.rate, INT32_MAX));
    }
}
} // namespace
S32 AIL_startup()
{
    last_error[0] = 0;
    return 1;
}
char* AIL_last_error()
{
    return last_error;
}
void* AIL_mem_alloc_lock(U32 size)
{
    return malloc(size);
}
void AIL_mem_free_lock(void* pointer)
{
    free(pointer);
}
S32 AIL_set_preference(U32 name, S32 value)
{
    int* setting = name == DIG_MIXER_CHANNELS ? &mixer_channels
                   : name == DIG_USE_WAVEOUT  ? &waveout
                                              : nullptr;
    if (!setting)
        throw std::runtime_error("Unsupported native audio preference");
    int old = *setting;
    *setting = value;
    return old;
}
HDIGDRIVER AIL_open_digital_driver(U32 frequency, S32 bits, S32 channels, U32 flags)
{
    if (active_driver || flags || (bits != 8 && bits != 16) || channels < 1 || channels > 2)
        return nullptr;
    auto driver = std::make_unique<_DIG_DRIVER>();
    auto config = ma_engine_config_init();
    config.sampleRate = frequency;
    config.channels = channels;
    config.noDevice = offline;
    if (record(ma_engine_init(&config, &driver->engine)) != MA_SUCCESS)
        return nullptr;
    driver->listener.driver = driver.get();
    driver->listener.listener = true;
    active_driver = driver.release();
    return active_driver;
}
void AIL_close_digital_driver(HDIGDRIVER driver)
{
    if (!driver)
        return;
    ma_engine_uninit(&driver->engine);
    if (driver == active_driver)
        active_driver = nullptr;
    delete driver;
}
void AIL_digital_configuration(HDIGDRIVER driver, S32* rate, S32* format, char* name)
{
    if (!driver)
        return;
    if (rate)
        *rate = ma_engine_get_sample_rate(&driver->engine);
    if (format)
        *format = ma_engine_get_channels(&driver->engine) == 2 ? 3 : 1;
    if (name)
        strcpy(name, "miniaudio native");
}
HSAMPLE AIL_allocate_sample_handle(HDIGDRIVER driver)
{
    if (!driver)
        return nullptr;
    auto sample = new _SAMPLE;
    sample->driver = driver;
    return sample;
}
void AIL_init_sample(HSAMPLE sample)
{
    if (sample)
        clear(*sample);
}
void AIL_release_sample_handle(HSAMPLE sample)
{
    delete sample;
}
S32 AIL_set_named_sample_file(HSAMPLE sample, const C8*, const void* bytes, S32 size, S32 block)
{
    return size > 0 && !block && memory(sample, bytes, size, false);
}
HSTREAM AIL_open_stream(HDIGDRIVER driver, const char* path, S32)
{
    if (!driver || !path)
        return nullptr;
    HWFILE file = FileOpen(const_cast<char*>(path), FILE_ACCESS_READ | FILE_OPEN_EXISTING, FALSE);
    if (!file)
        return nullptr;
    auto stream = std::make_unique<_STREAM>();
    stream->driver = driver;
    stream->source.length = FileGetSize(file);
    if (DB_EXTRACT_LIBRARY(file) == REAL_FILE_LIBRARY_ID)
    {
        FileClose(file);
        stream->source.file =
            W8CreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    else
    {
        stream->source.file = OpenLibraryStream(file);
        FileClose(file);
    }
    if (stream->source.file == INVALID_HANDLE_VALUE)
        return nullptr;
    LONG high = 0;
    DWORD low = W8SetFilePointer(stream->source.file, 0, &high, FILE_CURRENT);
    if (low == INVALID_SET_FILE_POINTER && W8GetLastError())
        return nullptr;
    stream->source.start = (uint64_t(uint32_t(high)) << 32) | low;
    auto config = ma_decoder_config_init(ma_format_f32, 0, 0);
    if (!attach(*stream, false,
                ma_decoder_init(read_file, seek_file, &stream->source, &config,
                                &stream->source.decoder)))
        return nullptr;
    return stream.release();
}
void AIL_close_stream(HSTREAM stream)
{
    delete stream;
}
S32 AIL_service_stream(HSTREAM stream, S32)
{
    return status(stream) == SMP_PLAYING;
}
S32 AIL_enumerate_3D_providers(HPROENUM* next, HPROVIDER* destination, C8** name)
{
    static char provider[] = "miniaudio spatial";
    if (!next || !destination || !name || *next)
        return 0;
    *next = 1;
    *destination = 1;
    *name = provider;
    return 1;
}
M3DRESULT AIL_open_3D_provider(HPROVIDER provider)
{
    return provider == 1 && active_driver ? M3D_NOERR : 7;
}
void AIL_close_3D_provider(HPROVIDER provider)
{
    if (provider != 1)
        throw std::runtime_error("Unknown native audio provider");
}
H3DPOBJECT AIL_open_3D_listener(HPROVIDER provider)
{
    return provider == 1 && active_driver ? &active_driver->listener : nullptr;
}
void AIL_3D_provider_attribute(HPROVIDER, const char*, void* value)
{
    if (value)
        *static_cast<S32*>(value) = -1;
}
void AIL_set_3D_provider_preference(HPROVIDER, const char*, const void*)
{
    throw std::runtime_error("Native EAX environments are unsupported");
}
H3DSAMPLE AIL_allocate_3D_sample_handle(HPROVIDER provider)
{
    if (provider != 1 || !active_driver)
        return nullptr;
    auto sample = new h3DPOBJECT;
    sample->driver = active_driver;
    return sample;
}
void AIL_release_3D_sample_handle(H3DSAMPLE sample)
{
    delete sample;
}
S32 W8AudioSetSpatialFile(H3DSAMPLE sample, const void* data, UINT32 size)
{
    return memory(sample, data, size, true);
}
void AIL_set_3D_position(H3DPOBJECT object, F32 x, F32 y, F32 z)
{
    if (!object)
        return;
    if (object->listener)
        ma_engine_listener_set_position(&object->driver->engine, 0, x, y, z);
    else if (object->initialized)
        ma_sound_set_position(&object->sound, x, y, z);
}
void AIL_set_3D_velocity_vector(H3DPOBJECT object, F32 x, F32 y, F32 z)
{
    if (!object)
        return;
    if (object->listener)
        ma_engine_listener_set_velocity(&object->driver->engine, 0, x * 1000, y * 1000, z * 1000);
    else if (object->initialized)
        ma_sound_set_velocity(&object->sound, x * 1000, y * 1000, z * 1000);
}
void AIL_set_3D_orientation(H3DPOBJECT object, F32 x, F32 y, F32 z, F32 ux, F32 uy, F32 uz)
{
    if (!object)
        return;
    if (object->listener)
    {
        ma_engine_listener_set_direction(&object->driver->engine, 0, x, y, z);
        ma_engine_listener_set_world_up(&object->driver->engine, 0, ux, uy, uz);
    }
    else if (object->initialized)
        ma_sound_set_direction(&object->sound, x, y, z);
}
void AIL_set_3D_sample_distances(H3DSAMPLE sample, F32 maximum, F32 minimum)
{
    if (sample && sample->initialized)
    {
        ma_sound_set_min_distance(&sample->sound, minimum);
        ma_sound_set_max_distance(&sample->sound, maximum);
    }
}
#define VOICE_FUNCTIONS(kind, Type)                                                                \
    void AIL_start_##kind(Type voice)                                                              \
    {                                                                                              \
        start(voice);                                                                              \
    }                                                                                              \
    void AIL_set_##kind##_volume(Type voice, S32 value)                                            \
    {                                                                                              \
        volume(voice, value);                                                                      \
    }                                                                                              \
    S32 AIL_##kind##_volume(Type voice)                                                            \
    {                                                                                              \
        return voice ? voice->volume : 0;                                                          \
    }                                                                                              \
    void AIL_set_##kind##_playback_rate(Type voice, S32 value)                                     \
    {                                                                                              \
        rate(voice, value);                                                                        \
    }                                                                                              \
    S32 AIL_##kind##_playback_rate(Type voice)                                                     \
    {                                                                                              \
        return voice ? voice->rate : 0;                                                            \
    }
VOICE_FUNCTIONS(sample, HSAMPLE)
VOICE_FUNCTIONS(stream, HSTREAM)
VOICE_FUNCTIONS(3D_sample, H3DSAMPLE)
void AIL_stop_sample(HSAMPLE voice)
{
    stop(voice);
}
void AIL_stop_3D_sample(H3DSAMPLE voice)
{
    stop(voice);
}
U32 AIL_sample_status(HSAMPLE voice)
{
    return status(voice);
}
U32 AIL_3D_sample_status(H3DSAMPLE voice)
{
    return status(voice);
}
S32 AIL_stream_status(HSTREAM voice)
{
    return status(voice);
}
void AIL_set_sample_pan(HSAMPLE voice, S32 value)
{
    pan(voice, value);
}
void AIL_set_stream_pan(HSTREAM voice, S32 value)
{
    pan(voice, value);
}
void AIL_set_sample_loop_count(HSAMPLE voice, S32 count)
{
    loop(voice, count);
}
void AIL_set_stream_loop_count(HSTREAM voice, S32 count)
{
    loop(voice, count);
}
void AIL_set_3D_sample_loop_count(H3DSAMPLE voice, U32 count)
{
    loop(voice, count);
}
void AIL_sample_ms_position(HSAMPLE voice, S32* total, S32* current)
{
    position(voice, total, current);
}
void AIL_stream_ms_position(HSTREAM voice, S32* total, S32* current)
{
    position(voice, total, current);
}
namespace w8_native
{
void audio_offline_for_test(bool enabled)
{
    if (active_driver)
        throw std::runtime_error("Cannot change audio mode while initialized");
    offline = enabled;
}
bool audio_render_for_test(float* samples, size_t frames)
{
    return active_driver && offline &&
           ma_engine_read_pcm_frames(&active_driver->engine, samples, frames, nullptr) ==
               MA_SUCCESS;
}
} // namespace w8_native

namespace w8_native
{
struct MovieAudio::State
{
    ma_data_source_base base{};
    ma_sound sound{};
    unsigned channels, rate;
    std::deque<float> queued;
    mutable std::mutex mutex;
    bool finished = false, initialized = false;
    static ma_result read(ma_data_source* data, void* output, ma_uint64 frames, ma_uint64* count)
    {
        auto& source = *reinterpret_cast<State*>(data);
        std::lock_guard<std::mutex> lock(source.mutex);
        size_t available = std::min<ma_uint64>(frames, source.queued.size() / source.channels);
        auto samples = static_cast<float*>(output);
        for (size_t i = 0; i < available * source.channels; ++i)
        {
            if (samples)
                samples[i] = source.queued.front();
            source.queued.pop_front();
        }
        *count = available;
        if (source.finished)
            return available == frames ? MA_SUCCESS : MA_AT_END;
        // An underrun is silence, not end-of-stream: decoding resumes on the main thread.
        if (samples)
            std::fill(samples + available * source.channels, samples + frames * source.channels,
                      0.f);
        *count = frames;
        return MA_SUCCESS;
    }
    static ma_result format(ma_data_source* data, ma_format* format, ma_uint32* channels,
                            ma_uint32* rate, ma_channel* map, size_t capacity)
    {
        auto& source = *reinterpret_cast<State*>(data);
        if (format)
            *format = ma_format_f32;
        if (channels)
            *channels = source.channels;
        if (rate)
            *rate = source.rate;
        if (map)
            ma_channel_map_init_standard(ma_standard_channel_map_default, map, capacity,
                                         source.channels);
        return MA_SUCCESS;
    }
    State(unsigned frequency, unsigned channel_count) : channels(channel_count), rate(frequency)
    {
        static const ma_data_source_vtable table = {read,    nullptr, format, nullptr,
                                                    nullptr, nullptr, 0};
        auto config = ma_data_source_config_init();
        config.vtable = &table;
        if (!active_driver || ma_data_source_init(&config, &base) != MA_SUCCESS)
            throw std::runtime_error("Movie audio has no initialized output engine");
        if (ma_sound_init_from_data_source(&active_driver->engine, &base,
                                           MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr,
                                           &sound) != MA_SUCCESS)
        {
            ma_data_source_uninit(&base);
            throw std::runtime_error("Cannot create movie PCM voice");
        }
        initialized = true;
    }
    ~State()
    {
        if (initialized)
        {
            // Uninit disconnects the engine before releasing callback-owned PCM/mutex.
            ma_sound_uninit(&sound);
            ma_data_source_uninit(&base);
        }
    }
};
MovieAudio::MovieAudio(unsigned rate, unsigned channels)
    : state(std::make_unique<State>(rate, channels))
{
}
MovieAudio::~MovieAudio() = default;
void MovieAudio::append(const float* samples, size_t frames)
{
    std::lock_guard<std::mutex> lock(state->mutex);
    if (frames > state->rate * 2 ||
        state->queued.size() / state->channels + frames > state->rate * 2)
        throw std::runtime_error("Movie audio exceeded two seconds of lookahead");
    state->queued.insert(state->queued.end(), samples, samples + frames * state->channels);
}
void MovieAudio::start()
{
    if (ma_sound_start(&state->sound) != MA_SUCCESS)
        throw std::runtime_error("Cannot start movie PCM voice");
}
void MovieAudio::finish()
{
    std::lock_guard<std::mutex> lock(state->mutex);
    state->finished = true;
}
bool MovieAudio::drained() const
{
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->finished && state->queued.empty();
}
bool movie_audio_available()
{
    return active_driver != nullptr;
}
} // namespace w8_native
