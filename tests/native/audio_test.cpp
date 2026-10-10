/* Native soundman -> SDL3_mixer, with deterministic offline mixing. */
#include "native/audio_test.h"
#include "wiz8/slf.h"
#include <wiz8/native_audio.h>
#include "native/movie_audio.h"
#include <wiz8/filesystem.h>
#include <wiz8/asset_paths.h>
#include "soundman.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "temporary_directory.h"
#include <vector>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static unsigned callbacks = 0;
static void ended(void* data)
{
    ++callbacks;
    ++*static_cast<unsigned*>(data);
}
struct RestartOnEnd
{
    char* path;
    SOUNDPARMS options;
    UINT32 restarted = SOUND_ERROR;
    unsigned calls = 0;
    static void callback(void* data)
    {
        auto& restart = *static_cast<RestartOnEnd*>(data);
        ++restart.calls;
        restart.restarted = SoundPlay(restart.path, &restart.options);
    }
};
static void word(std::vector<unsigned char>& data, unsigned value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        data.push_back(value >> (i * 8));
}
static void tag(std::vector<unsigned char>& data, const char* value)
{
    data.insert(data.end(), value, value + 4);
}
static std::vector<unsigned char> wav(bool stereo = false)
{
    std::vector<unsigned char> bytes;
    tag(bytes, "RIFF");
    word(bytes, 36 + 4410 * (stereo ? 4 : 2), 4);
    tag(bytes, "WAVE");
    tag(bytes, "fmt ");
    word(bytes, 16, 4);
    word(bytes, 1, 2);
    word(bytes, stereo ? 2 : 1, 2);
    word(bytes, 44100, 4);
    word(bytes, stereo ? 176400 : 88200, 4);
    word(bytes, stereo ? 4 : 2, 2);
    word(bytes, 16, 2);
    tag(bytes, "data");
    word(bytes, 4410 * (stereo ? 4 : 2), 4);
    for (int i = 0; i < 4410; ++i)
    {
        word(bytes, unsigned(short(std::sin(i * 2 * 3.14159265 * 440 / 44100) * 12000)), 2);
        if (stereo)
            word(bytes, unsigned(short(std::sin(i * 2 * 3.14159265 * 880 / 44100) * 6000)), 2);
    }
    return bytes;
}
static void write(const std::filesystem::path& name, const std::vector<unsigned char>& bytes)
{
    std::ofstream file(name, std::ios::binary);
    if (!file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()))
        abort();
}
static std::vector<float> mix(size_t frames)
{
    std::vector<float> samples(frames * 2);
    if (!w8_native::audio_render_for_test(samples.data(), frames))
        abort();
    return samples;
}
static double energy(const std::vector<float>& values, int channel)
{
    double sum = 0;
    for (size_t i = channel; i < values.size(); i += 2)
        sum += values[i] * values[i];
    return sum;
}
static unsigned crossings(const std::vector<float>& samples, int channel = 0)
{
    unsigned count = 0;
    for (size_t i = channel + 2; i < samples.size(); i += 2)
        count += samples[i - 2] <= 0 && samples[i] > 0;
    return count;
}
static bool near(double value, double expected, double tolerance = 0.02)
{
    return std::abs(value - expected) <= tolerance * std::max(1.0, std::abs(expected));
}
int main(int argc, char**)
{
    const auto temporary = make_temporary_directory("wiz8-audio");
    auto asset = std::filesystem::path(temporary) / "assets",
         user = std::filesystem::path(temporary) / "user";
    std::filesystem::create_directories(asset);
    std::filesystem::create_directories(user);
    w8_native::configure_paths({asset.string(), user.string(), {}});
    std::filesystem::copy_file(WIZ8_AUDIO_TEST_MP3, asset / "fallback.mp3");
    auto wave = wav();
    write(asset / "tone.wav", wave);
    write(asset / "stereo.wav", wav(true));
    write(asset / "bad.wav", {'n', 'o', 't', 'a', 'w', 'a', 'v'});
    std::filesystem::create_directories(asset / "Data");
    wiz8::SlfHeader header{};
    strcpy(header.sLibName, "Data.slf");
    strcpy(header.sPathToLibrary, "Data\\");
    header.iEntries = header.iUsed = 2;
    header.iVersion = 0x200;
    wiz8::SlfEntry entries[2]{};
    strcpy(entries[0].sFileName, "Packed.wav");
    entries[0].uiOffset = sizeof(header);
    entries[0].uiLength = wave.size();
    strcpy(entries[1].sFileName, "Truncated.wav");
    entries[1].uiOffset = sizeof(header);
    entries[1].uiLength = 16;
    std::vector<unsigned char> archive(reinterpret_cast<unsigned char*>(&header),
                                       reinterpret_cast<unsigned char*>(&header) + sizeof(header));
    archive.insert(archive.end(), wave.begin(), wave.end());
    archive.insert(archive.end(), reinterpret_cast<unsigned char*>(entries),
                   reinterpret_cast<unsigned char*>(entries) + sizeof(entries));
    write(asset / "Data" / "DATA.SLF", archive);

    wiz8::mount_slf("Data\\Data.slf");
    {
        char packed[] = "data\\PACKED.WAV";
        auto entry = [&]() { try { return wiz8::open_file(packed, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
        CHECK(entry);
        auto first = wiz8::open_file(packed), second = wiz8::open_file(packed);
        entry.reset();
        CHECK(first && second && first->tell() == 0 && second->tell() == 0);
        auto loose = wiz8::open_file("C:\\TONE.wav");
        unsigned char bytes[64]{};
        CHECK(first->read(bytes, sizeof(bytes)).bytes == sizeof(bytes));
        CHECK(std::memcmp(bytes, wave.data(), sizeof(bytes)) == 0);
        CHECK(second->tell() == 0 && loose->tell() == 0);
        CHECK(second->read(bytes, 32).bytes == 32);
        CHECK(std::memcmp(bytes, wave.data(), 32) == 0);
        first.reset();
        CHECK(second->seek(wave.size() - 32, wiz8::SeekOrigin::begin) ==
              int64_t(wave.size() - 32));
        CHECK(second->read(bytes, 32).bytes == 32);
        CHECK(std::memcmp(bytes, wave.data() + wave.size() - 32, 32) == 0);
        CHECK(loose->read(bytes, sizeof(bytes)).bytes == sizeof(bytes));
        CHECK(std::memcmp(bytes, wave.data(), sizeof(bytes)) == 0);
    }
    w8_native::audio_offline_for_test(true);
    CHECK(InitializeSoundManager());
    {
        auto looseInput = w8_native::open_audio_input("C:\\tone.wav");
        auto packedInput = w8_native::open_audio_input("Data\\Packed.wav");
        auto loopInput = w8_native::open_audio_input("data\\PACKED.WAV");
        auto badInput = w8_native::open_audio_input("Data\\Truncated.wav");
        CHECK(looseInput && packedInput && loopInput && badInput);
        CHECK(SDL_GetIOSize(packedInput.get()) == Sint64(wave.size()));
        CHECK(SDL_SeekIO(packedInput.get(), wave.size() + 1, SDL_IO_SEEK_SET) == -1);
        CHECK(SDL_SeekIO(packedInput.get(), 0, SDL_IO_SEEK_SET) == 0);
        CHECK(!w8_native::open_audio_input("missing.wav"));
        w8_native::Track loose(MIX_CreateTrack(w8_native::audio_mixer()));
        w8_native::Track packed(MIX_CreateTrack(w8_native::audio_mixer()));
        w8_native::Track looping(MIX_CreateTrack(w8_native::audio_mixer()));
        w8_native::Track bad(MIX_CreateTrack(w8_native::audio_mixer()));
        CHECK(loose && packed && looping && bad);
        CHECK(MIX_SetTrackIOStream(loose.get(), looseInput.get(), false));
        CHECK(MIX_SetTrackIOStream(packed.get(), packedInput.get(), false));
        CHECK(MIX_SetTrackIOStream(looping.get(), loopInput.get(), false));
        CHECK(!MIX_SetTrackIOStream(bad.get(), badInput.get(), false));
        for (auto track : {loose.get(), packed.get()})
        {
            CHECK(MIX_GetTrackPlaybackPosition(track) == 0);
            CHECK(MIX_PlayTrack(track, 0));
            CHECK(MIX_TrackFramesToMS(track, MIX_GetTrackRemaining(track)) == 100);
        }
        auto properties = SDL_CreateProperties();
        CHECK(SDL_SetNumberProperty(properties, MIX_PROP_PLAY_LOOPS_NUMBER, -1));
        CHECK(MIX_PlayTrack(looping.get(), properties));
        SDL_DestroyProperties(properties);
        auto samples = mix(8192);
        CHECK(energy(samples, 0) + energy(samples, 1) > 10);
        CHECK(!MIX_TrackPlaying(loose.get()));
        CHECK(!MIX_TrackPlaying(packed.get()));
        CHECK(MIX_TrackPlaying(looping.get()));
        loose.reset();
        packed.reset();
        samples = mix(8192);
        CHECK(MIX_TrackPlaying(looping.get()));
        CHECK(energy(samples, 0) + energy(samples, 1) > 10);
    }
    SOUNDPARMS options;
    memset(&options, 0xff, sizeof(options));
    unsigned callback_data = 0;
    options.uiVolume = 127;
    options.uiPan = 0;
    options.uiLoop = 1;
    options.uiPriority = 10;
    options.EOSCallback = ended;
    options.pCallbackData = &callback_data;
    char path[] = "TONE.WAV";
    auto sound = SoundPlay(path, &options);
    CHECK(sound != SOUND_ERROR && SoundIsPlaying(sound));
    CHECK(SoundFileIsPlaying(path));
    UINT32 total, current;
    CHECK(SoundGetMilliSecondPosition(sound, &total, &current));
    CHECK(total == 100);
    auto samples = mix(2048);
    CHECK(energy(samples, 0) > 10 && energy(samples, 1) < 0.001);
    CHECK(SoundGetMilliSecondPosition(sound, &total, &current));
    CHECK(current > 0 && current <= 100);
    CHECK(SoundGetVolume(sound) == 127);
    CHECK(SoundSetVolume(sound, 64));
    CHECK(SoundGetVolume(sound) == 64);
    mix(8192);
    CHECK(!SoundIsPlaying(sound));
    SoundServiceStreams();
    CHECK(callbacks == 1 && callback_data == 1);
    SoundServiceStreams();
    CHECK(callbacks == 1);
    /* The all-ones callback sentinel must not become a callable LP64 address. */
    memset(&options, 0xff, sizeof(options));
    options.uiLoop = 2;
    sound = SoundPlay(path, &options);
    CHECK(sound != SOUND_ERROR);
    mix(6000);
    CHECK(SoundIsPlaying(sound));
    mix(6000);
    SoundServiceStreams();
    CHECK(!SoundIsPlaying(sound));
    options.uiLoop = 0;
    options.uiVolume = 127;
    sound = SoundPlayStreamedFile(path, &options);
    CHECK(sound != SOUND_ERROR);
    mix(12000);
    CHECK(SoundIsPlaying(sound));
    SoundSetMusic(sound);
    CHECK(SoundStopMusic());
    CHECK(!SoundIsPlaying(sound));
    sound = SoundPlay(path, &options);
    CHECK(sound != SOUND_ERROR);
    CHECK(SoundSetFadeVolume(sound, 0, 0, TRUE));
    for (int i = 0; i < 130; ++i)
        SoundServiceStreams();
    CHECK(!SoundIsPlaying(sound));
    options.uiLoop = 0;
    options.uiPriority = 42;
    auto first = SoundPlay(path, &options), second = SoundPlayStreamedFile(path, &options);
    CHECK(first != SOUND_ERROR && second != SOUND_ERROR);
    CHECK(SoundStopGroup(42));
    CHECK(!SoundIsPlaying(first) && !SoundIsPlaying(second));
    SOUND3DPARMS spatial;
    memset(&spatial, 0xff, sizeof(spatial));
    memset(&spatial.Pos, 0, sizeof(spatial.Pos));
    spatial.Pos.flZ = -3;
    spatial.Pos.flFaceZ = -1;
    spatial.Pos.flUpY = 1;
    spatial.uiVolume = 127;
    spatial.uiLoop = 1;
    sound = Sound3DPlay(path, &spatial);
    CHECK(sound != SOUND_ERROR && SoundIsPlaying(sound));
    samples = mix(1024);
    CHECK(energy(samples, 0) + energy(samples, 1) > 1);
    Sound3DSetPosition(sound, 1, 0, -3);
    Sound3DSetDirection(sound, 0, 0, -1, 0, 1, 0);
    CHECK(SoundStop(sound));
    CHECK(SoundEmptyCache());
    char packed[] = "Data\\Packed.wav";
    memset(&options, 0xff, sizeof(options));
    options.uiLoop = 1;
    sound = SoundPlayStreamedFile(packed, &options);
    CHECK(sound != SOUND_ERROR);
    CHECK(SoundGetMilliSecondPosition(sound, &total, &current) && total == 100);
    samples = mix(8192);
    CHECK(energy(samples, 0) + energy(samples, 1) > 10);
    SoundServiceStreams();
    CHECK(!SoundIsPlaying(sound));
    options.uiLoop = 0;
    options.uiPriority = 123;
    for (int i = 0; i < 32; ++i)
        CHECK(SoundPlay(path, &options) != SOUND_ERROR);
    CHECK(SoundPlay(path, &options) == SOUND_ERROR);
    CHECK(SoundStopGroup(123));
    RANDOMPARMS random;
    memset(&random, 0xff, sizeof(random));
    random.uiTimeMin = random.uiTimeMax = 0;
    auto random_id = SoundPlayRandom(path, &random);
    CHECK(random_id != SOUND_ERROR);
    CHECK(SoundRandomShouldPlay(random_id));
    CHECK(SoundStartRandom(random_id) != SOUND_ERROR);
    CHECK(!SoundRandomShouldPlay(random_id));
    mix(8192);
    SoundServiceStreams();
    CHECK(SoundRandomShouldPlay(random_id));
    /* The recovered function clears flags but always returns FALSE. */
    CHECK(!SoundStopAllRandom());
    CHECK(!SoundRandomShouldPlay(random_id));
    CHECK(SoundEmptyCache());
    memset(&options, 0xff, sizeof(options));
    options.uiLoop = 0;
    sound = SoundPlay(path, &options);
    CHECK(sound != SOUND_ERROR);
    SoundSetMusic(sound);
    CHECK(SoundEmptyCache());
    samples = mix(12000);
    CHECK(SoundIsPlaying(sound));
    CHECK(energy(samples, 0) + energy(samples, 1) > 10);
    CHECK(SoundStopMusic());
    // Old backend: 2048 frames at full gain = 137.670993 per ear; front = 1/4 power.
    const double baseline = 137.670993;
    spatial.uiLoop = 0;
    spatial.Pos.flZ = 0;
    for (auto x : {-3.0f, 0.0f, 3.0f})
    {
        spatial.Pos.flX = x;
        sound = Sound3DPlay(path, &spatial);
        CHECK(sound != SOUND_ERROR);
        samples = mix(2048);
        const auto left = energy(samples, 0), right = energy(samples, 1);
        CHECK(near(left, baseline * (x > 0 ? 0.04 : 1)));
        CHECK(near(right, baseline * (x < 0 ? 0.04 : 1)));
        CHECK(SoundStop(sound));
    }
    spatial.Pos.flX = 0;
    spatial.Pos.flFalloffMin = 1;
    spatial.Pos.flFalloffMax = 10;
    for (auto distance : {1.0f, 100.0f})
    {
        spatial.Pos.flZ = -distance;
        sound = Sound3DPlay(path, &spatial);
        CHECK(sound != SOUND_ERROR);
        samples = mix(2048);
        CHECK(near(energy(samples, 0), baseline * 0.25));
        CHECK(near(energy(samples, 1), baseline * 0.25));
        CHECK(SoundStop(sound));
    }
    spatial.Pos.flX = 3; spatial.Pos.flZ = 0;
    Sound3DSetListenerOrientation(0, 0, 1, 0, 1, 0);
    sound = Sound3DPlay(path, &spatial);
    CHECK(sound != SOUND_ERROR);
    samples = mix(2048);
    CHECK(near(energy(samples, 0), baseline) && near(energy(samples, 1), baseline * 0.04));
    Sound3DSetListenerOrientation(0, 0, -1, 0, 1, 0);
    samples = mix(2048);
    CHECK(energy(samples, 1) > energy(samples, 0) * 20);
    Sound3DSetListener(6, 0, 0);
    samples = mix(2048);
    CHECK(energy(samples, 0) > energy(samples, 1) * 20);
    CHECK(SoundStop(sound));
    Sound3DSetListener(0, 0, 0);
    // Velocity was normally zero at game callers; retain OpenAL-style doppler for nonzero inputs.
    spatial.Pos.flVelX = -100;
    spatial.uiLoop = 1;
    sound = Sound3DPlay(path, &spatial);
    CHECK(sound != SOUND_ERROR);
    samples = mix(2048);
    CHECK(crossings(samples, 1) >= 27 && crossings(samples, 1) <= 30);
    CHECK(SoundStop(sound));
    spatial.Pos.flVelX = 0;
    Sound3DSetListenerVelocity(100, 0, 0);
    sound = Sound3DPlay(path, &spatial);
    CHECK(sound != SOUND_ERROR);
    samples = mix(2048);
    CHECK(crossings(samples, 1) >= 25 && crossings(samples, 1) <= 28);
    CHECK(SoundStop(sound));
    Sound3DSetListenerVelocity(0, 0, 0);

    memset(&options, 0xff, sizeof(options));
    options.uiLoop = 1;
    options.uiVolume = 127;
    sound = SoundPlay(path, &options);
    CHECK(sound != SOUND_ERROR);
    samples = mix(2048);
    CHECK(near(energy(samples, 0), baseline) && near(energy(samples, 1), baseline));
    CHECK(SoundStop(sound));
    options.uiVolume = 64;
    sound = SoundPlay(path, &options);
    CHECK(sound != SOUND_ERROR);
    samples = mix(2048);
    CHECK(near(energy(samples, 0), baseline * std::pow(64.0 / 127, 2)));
    CHECK(SoundStop(sound));
    options.uiVolume = 127;
    options.uiSpeed = 88200;
    sound = SoundPlay(path, &options);
    CHECK(sound != SOUND_ERROR);
    samples = mix(2048);
    CHECK(crossings(samples) >= 40 && crossings(samples) <= 42);
    mix(1024);
    CHECK(!SoundIsPlaying(sound));
    SoundServiceStreams();
    options.uiSpeed = 22050;
    sound = SoundPlayStreamedFile(path, &options);
    CHECK(sound != SOUND_ERROR);
    samples = mix(2048);
    CHECK(crossings(samples) >= 10 && crossings(samples) <= 11);
    mix(4000);
    CHECK(SoundIsPlaying(sound));
    mix(4000);
    CHECK(!SoundIsPlaying(sound));
    SoundServiceStreams();
    options.uiSpeed = SOUND_ERROR;
    options.uiLoop = 2;
    sound = SoundPlayStreamedFile(packed, &options);
    CHECK(sound != SOUND_ERROR);
    mix(6000);
    CHECK(SoundIsPlaying(sound));
    mix(6000);
    SoundServiceStreams();
    CHECK(!SoundIsPlaying(sound));
    char stereo[] = "stereo.wav";
    options.uiLoop = 1;
    sound = SoundPlay(stereo, &options);
    CHECK(sound != SOUND_ERROR);
    samples = mix(2048);
    CHECK(near(energy(samples, 1) / energy(samples, 0), 0.25));
    CHECK(SoundStop(sound));
    options.uiPan = 127;
    sound = SoundPlay(stereo, &options);
    CHECK(sound != SOUND_ERROR);
    samples = mix(2048);
    CHECK(energy(samples, 0) < 0.001 && near(energy(samples, 1), baseline * 0.25));
    CHECK(SoundStop(sound));
    char fallback[] = "fallback.wav";
    options.uiPan = 64;
    sound = SoundPlay(fallback, &options);
    CHECK(sound != SOUND_ERROR);
    samples = mix(8192);
    CHECK(energy(samples, 0) + energy(samples, 1) > 1);
    SoundServiceStreams();
    CHECK(!SoundIsPlaying(sound));
    char bad[] = "bad.wav", truncated[] = "Data\\Truncated.wav";
    CHECK(SoundPlay(bad, &options) == SOUND_ERROR);
    CHECK(SoundPlayStreamedFile(bad, &options) == SOUND_ERROR);
    CHECK(SoundPlay(truncated, &options) == SOUND_ERROR);
    CHECK(SoundPlayStreamedFile(truncated, &options) == SOUND_ERROR);
    CHECK(SoundPlay(nullptr, &options) == SOUND_ERROR);
    CHECK(SoundPlayStreamedFile(nullptr, &options) == SOUND_ERROR);
    CHECK(!SoundRandomShouldPlay(NO_SAMPLE));
    CHECK(SoundStartRandom(NO_SAMPLE) == SOUND_ERROR);
    sound = SoundPlayStreamedFile(fallback, &options);
    CHECK(sound != SOUND_ERROR);
    samples = mix(8192);
    CHECK(energy(samples, 0) + energy(samples, 1) > 1);
    SoundServiceStreams();
    CHECK(!SoundIsPlaying(sound));
    RestartOnEnd restart{path, options};
    restart.options.EOSCallback = nullptr;
    options.EOSCallback = RestartOnEnd::callback;
    options.pCallbackData = &restart;
    sound = SoundPlay(path, &options);
    CHECK(sound != SOUND_ERROR);
    mix(8192);
    SoundServiceStreams();
    CHECK(restart.calls == 1 && restart.restarted != SOUND_ERROR && SoundIsPlaying(restart.restarted));
    SoundServiceStreams();
    CHECK(restart.calls == 1);
    CHECK(SoundStop(restart.restarted));
    options.EOSCallback = nullptr;

    CHECK(SoundEmptyCache());
    {
        CHECK(w8_native::movie_audio_available());
        w8_native::MovieAudio movie(44100, 1);
        movie.start();
        samples = mix(1024);
        CHECK(energy(samples, 0) + energy(samples, 1) == 0);
        std::vector<float> pcm(4096);
        for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = float(std::sin(i * 2 * 3.14159265 * 440 / 44100) * 0.25);
        movie.append(pcm.data(), pcm.size());
        samples = mix(2048);
        CHECK(energy(samples, 0) + energy(samples, 1) > 1);
        mix(8192);
        movie.append(pcm.data(), pcm.size());
        movie.finish();
        CHECK(!movie.drained());
        mix(8192);
        CHECK(movie.drained());
        // No second mixer when soundman is reset while a movie is still alive.
        const auto* sharedMixer = w8_native::audio_mixer();
        ShutdownSoundManager();
        CHECK(InitializeSoundManager());
        CHECK(sharedMixer == w8_native::audio_mixer());
    }
    {
        w8_native::MovieAudio movie(48000, 2);
        std::vector<float> pcm(4800 * 2, 0.1f);
        movie.append(pcm.data(), pcm.size() / 2);
        movie.start();
        samples = mix(1024);
        CHECK(energy(samples, 0) > 1 && energy(samples, 1) > 1);
        movie.finish();
        mix(8192);
        CHECK(movie.drained());
        bool rejected = false;
        try { movie.append(pcm.data(), pcm.size() / 2); }
        catch (const std::exception&) { rejected = true; }
        CHECK(rejected);
    }
    {
        w8_native::MovieAudio movie(44100, 1);
        std::vector<float> pcm(4096, 0.1f);
        movie.append(pcm.data(), pcm.size());
        movie.start();
        mix(1024);
        // Destroy an active queued movie, then mix again to catch stale input access.
    }
    samples = mix(8192);
    CHECK(energy(samples, 0) + energy(samples, 1) == 0);
    unsigned before_shutdown = callbacks;
    options.EOSCallback = ended;
    options.pCallbackData = &callback_data;
    CHECK(SoundPlay(path, &options) != SOUND_ERROR);
    ShutdownSoundManager();
    CHECK(callbacks == before_shutdown);
    ShutdownSoundManager();
    CHECK(InitializeSoundManager());
    ShutdownSoundManager();
    if (argc > 1)
    {
        w8_native::audio_offline_for_test(false);
        CHECK(InitializeSoundManager());
        CHECK(w8_native::audio_mixer());
        memset(&options, 0xff, sizeof(options));
        options.uiLoop = 1;
        sound = SoundPlay(path, &options);
        CHECK(sound != SOUND_ERROR);
        SDL_Delay(250);
        SoundServiceStreams();
        CHECK(!SoundIsPlaying(sound));
        ShutdownSoundManager();
        puts("ok: native output device opened and completed playback");
    }
    wiz8::clear_asset_archives();

    std::filesystem::remove_all(temporary);
    puts("ok: native audio samples, streams, loops, pans, fades, callbacks, spatialization and "
         "lifetime");
}
