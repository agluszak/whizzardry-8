#include <catch2/catch_test_macros.hpp>
#include <ios>
#include <wiz8/native_audio.h>
#include <wiz8/filesystem.h>
#include <wiz8/asset_paths.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

/* Native soundman -> SDL3_mixer, with deterministic offline mixing. */
#include "native/audio_test.h"
#include "wiz8/slf.h"
#include "wiz8/local_code/CombatSound.h"
#include "native/movie_audio.h"
#include "soundman.h"
#include "temporary_directory.h"
#include "Types.h"
#include "catch2/catch_message.hpp"
#include "audio_resources.h"

struct AudioClip;
struct AudioPlayback;

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
    SoundPlayback restarted;
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
        FAIL("Audio fixture or offline mixer failed");
}
static std::vector<float> mix(size_t frames)
{
    std::vector<float> samples(frames * 2);
    if (!w8_native::audio_render_for_test(samples.data(), frames))
        FAIL("Audio fixture or offline mixer failed");
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


struct AudioFixture
{
    ScopedTemporaryDirectory directory{"wiz8-audio"};
    std::filesystem::path temporary = directory.path;
    std::filesystem::path asset = temporary / "assets";
    std::filesystem::path user = temporary / "user";
    std::vector<unsigned char> wave = wav();

    AudioFixture()
    {
        callbacks = 0;
        std::filesystem::create_directories(asset);
        std::filesystem::create_directories(user);
        w8_native::configure_paths({asset.string(), user.string(), {}});
        std::filesystem::copy_file(WIZ8_AUDIO_TEST_MP3, asset / "fallback.mp3");
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
    }

    void checkArchiveStreams()
    {
        {
            char packed[] = "data\\PACKED.WAV";
            auto entry = [&]() { try { return wiz8::open_file(packed, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
            REQUIRE(entry);
            auto first = wiz8::open_file(packed), second = wiz8::open_file(packed);
            entry.reset();
            REQUIRE((first && second && first->tell() == 0 && second->tell() == 0));
            auto loose = wiz8::open_file("C:\\TONE.wav");
            unsigned char bytes[64]{};
            REQUIRE(first->read(bytes, sizeof(bytes)).bytes == sizeof(bytes));
            REQUIRE(std::memcmp(bytes, wave.data(), sizeof(bytes)) == 0);
            REQUIRE((second->tell() == 0 && loose->tell() == 0));
            REQUIRE(second->read(bytes, 32).bytes == 32);
            REQUIRE(std::memcmp(bytes, wave.data(), 32) == 0);
            first.reset();
            REQUIRE(second->seek(wave.size() - 32, wiz8::SeekOrigin::begin) ==
                  int64_t(wave.size() - 32));
            REQUIRE(second->read(bytes, 32).bytes == 32);
            REQUIRE(std::memcmp(bytes, wave.data() + wave.size() - 32, 32) == 0);
            REQUIRE(loose->read(bytes, sizeof(bytes)).bytes == sizeof(bytes));
            REQUIRE(std::memcmp(bytes, wave.data(), sizeof(bytes)) == 0);
        }
    }

    void startOfflineAudio()
    {
        w8_native::audio_offline_for_test(true);
        REQUIRE(InitializeSoundManager());
    }

    ~AudioFixture()
    {
        ShutdownSoundManager();
        wiz8::clear_asset_archives();
    }
};

TEST_CASE_METHOD(AudioFixture, "Audio archives, tracks and combat sounds", "[audio][streams]")
{
    checkArchiveStreams();
    startOfflineAudio();
    // Variant selection accepts a bounded, read-only name and does not append
    // into its backing storage or consume the characters beyond the view.
    const auto combat = asset / "Data" / "Sound" / "Combat";
    std::filesystem::create_directories(combat);
    write(combat / "strike1.wav", wave);
    write(combat / "strike2.wav", wave);
    const std::string backing = "striketrailing";
    for (int repeat = 0; repeat < 2; ++repeat) {
        PlayCombatSound(std::string_view(backing).substr(0, 6), 2, false, 0);
        const auto rendered = mix(8192);
        CHECK(energy(rendered, 0) + energy(rendered, 1) > 10);
        CHECK(backing == "striketrailing");
    }
    {
        auto looseInput = w8_native::open_audio_input("C:\\tone.wav");
        auto packedInput = w8_native::open_audio_input("Data\\Packed.wav");
        auto loopInput = w8_native::open_audio_input("data\\PACKED.WAV");
        auto badInput = w8_native::open_audio_input("Data\\Truncated.wav");
        REQUIRE((looseInput && packedInput && loopInput && badInput));
        REQUIRE(SDL_GetIOSize(packedInput.get()) == Sint64(wave.size()));
        REQUIRE(SDL_SeekIO(packedInput.get(), wave.size() + 1, SDL_IO_SEEK_SET) == -1);
        REQUIRE(SDL_SeekIO(packedInput.get(), 0, SDL_IO_SEEK_SET) == 0);
        REQUIRE(!w8_native::open_audio_input("missing.wav"));
        w8_native::Track loose(MIX_CreateTrack(w8_native::audio_mixer()));
        w8_native::Track packed(MIX_CreateTrack(w8_native::audio_mixer()));
        w8_native::Track looping(MIX_CreateTrack(w8_native::audio_mixer()));
        w8_native::Track bad(MIX_CreateTrack(w8_native::audio_mixer()));
        REQUIRE((loose && packed && looping && bad));
        REQUIRE(MIX_SetTrackIOStream(loose.get(), looseInput.get(), false));
        REQUIRE(MIX_SetTrackIOStream(packed.get(), packedInput.get(), false));
        REQUIRE(MIX_SetTrackIOStream(looping.get(), loopInput.get(), false));
        REQUIRE(!MIX_SetTrackIOStream(bad.get(), badInput.get(), false));
        for (auto track : {loose.get(), packed.get()})
        {
            REQUIRE(MIX_GetTrackPlaybackPosition(track) == 0);
            REQUIRE(MIX_PlayTrack(track, 0));
            REQUIRE(MIX_TrackFramesToMS(track, MIX_GetTrackRemaining(track)) == 100);
        }
        auto properties = SDL_CreateProperties();
        REQUIRE(SDL_SetNumberProperty(properties, MIX_PROP_PLAY_LOOPS_NUMBER, -1));
        REQUIRE(MIX_PlayTrack(looping.get(), properties));
        SDL_DestroyProperties(properties);
        auto samples = mix(8192);
        REQUIRE(energy(samples, 0) + energy(samples, 1) > 10);
        REQUIRE(!MIX_TrackPlaying(loose.get()));
        REQUIRE(!MIX_TrackPlaying(packed.get()));
        REQUIRE(MIX_TrackPlaying(looping.get()));
        loose.reset();
        packed.reset();
        samples = mix(8192);
        REQUIRE(MIX_TrackPlaying(looping.get()));
        REQUIRE(energy(samples, 0) + energy(samples, 1) > 10);
    }

}

TEST_CASE_METHOD(AudioFixture, "Audio playback, spatialization and movie lifetime", "[audio][mixer]")
{
    startOfflineAudio();
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
    REQUIRE((sound != nullptr && SoundIsPlaying(sound)));
    REQUIRE(SoundFileIsPlaying(path));
    auto position = SoundGetMilliSecondPosition(sound);
    REQUIRE(position.has_value());
    REQUIRE(position->total_ms == 100);
    auto samples = mix(2048);
    REQUIRE((energy(samples, 0) > 10 && energy(samples, 1) < 0.001));
    position = SoundGetMilliSecondPosition(sound);
    REQUIRE(position.has_value());
    REQUIRE((position->current_ms > 0 && position->current_ms <= 100));
    REQUIRE(SoundGetVolume(sound) == 127);
    REQUIRE(SoundSetVolume(sound, 64));
    REQUIRE(SoundGetVolume(sound) == 64);
    mix(8192);
    REQUIRE(!SoundIsPlaying(sound));
    SoundServiceStreams();
    REQUIRE((callbacks == 1 && callback_data == 1));
    SoundServiceStreams();
    REQUIRE(callbacks == 1);
    /* The all-ones callback sentinel must not become a callable LP64 address. */
    memset(&options, 0xff, sizeof(options));
    options.uiLoop = 2;
    sound = SoundPlay(path, &options);
    REQUIRE(sound != nullptr);
    mix(6000);
    REQUIRE(SoundIsPlaying(sound));
    mix(6000);
    SoundServiceStreams();
    REQUIRE(!SoundIsPlaying(sound));
    options.uiLoop = 0;
    options.uiVolume = 127;
    sound = SoundPlayStreamedFile(path, &options);
    REQUIRE(sound != nullptr);
    mix(12000);
    REQUIRE(SoundIsPlaying(sound));
    SoundSetMusic(sound);
    REQUIRE(SoundStopMusic());
    REQUIRE(!SoundIsPlaying(sound));
    sound = SoundPlay(path, &options);
    REQUIRE(sound != nullptr);
    REQUIRE(SoundSetFadeVolume(sound, 0, 0, TRUE));
    for (int i = 0; i < 130; ++i)
        SoundServiceStreams();
    REQUIRE(!SoundIsPlaying(sound));
    options.uiLoop = 0;
    options.uiPriority = 42;
    auto first = SoundPlay(path, &options), second = SoundPlayStreamedFile(path, &options);
    REQUIRE((first != nullptr && second != nullptr));
    REQUIRE(SoundStopGroup(42));
    REQUIRE((!SoundIsPlaying(first) && !SoundIsPlaying(second)));
    SOUND3DPARMS spatial;
    memset(&spatial, 0xff, sizeof(spatial));
    memset(&spatial.Pos, 0, sizeof(spatial.Pos));
    spatial.Pos.flZ = -3;
    spatial.Pos.flFaceZ = -1;
    spatial.Pos.flUpY = 1;
    spatial.uiVolume = 127;
    spatial.uiLoop = 1;
    sound = Sound3DPlay(path, &spatial);
    REQUIRE((sound != nullptr && SoundIsPlaying(sound)));
    samples = mix(1024);
    REQUIRE(energy(samples, 0) + energy(samples, 1) > 1);
    Sound3DSetPosition(sound, 1, 0, -3);
    Sound3DSetDirection(sound, 0, 0, -1, 0, 1, 0);
    REQUIRE(SoundStop(sound));
    REQUIRE(SoundEmptyCache());
    char packed[] = "Data\\Packed.wav";
    memset(&options, 0xff, sizeof(options));
    options.uiLoop = 1;
    sound = SoundPlayStreamedFile(packed, &options);
    REQUIRE(sound != nullptr);
    position = SoundGetMilliSecondPosition(sound);
    REQUIRE((position.has_value() && position->total_ms == 100));
    samples = mix(8192);
    REQUIRE(energy(samples, 0) + energy(samples, 1) > 10);
    SoundServiceStreams();
    REQUIRE(!SoundIsPlaying(sound));
    options.uiLoop = 0;
    options.uiPriority = 123;
    for (int i = 0; i < 64; ++i)
        REQUIRE(SoundPlay(path, &options) != nullptr);
    REQUIRE(SoundStopGroup(123));
    RANDOMPARMS random;
    memset(&random, 0xff, sizeof(random));
    random.uiTimeMin = random.uiTimeMax = 0;
    auto random_id = SoundPlayRandom(path, &random);
    REQUIRE(random_id != nullptr);
    REQUIRE(SoundRandomShouldPlay(random_id));
    REQUIRE(SoundStartRandom(random_id) != nullptr);
    REQUIRE(!SoundRandomShouldPlay(random_id));
    mix(8192);
    SoundServiceStreams();
    REQUIRE(SoundRandomShouldPlay(random_id));
    /* The recovered function clears flags but always returns FALSE. */
    REQUIRE(!SoundStopAllRandom());
    REQUIRE(!SoundRandomShouldPlay(random_id));
    REQUIRE(SoundEmptyCache());
    memset(&options, 0xff, sizeof(options));
    options.uiLoop = 0;
    sound = SoundPlay(path, &options);
    REQUIRE(sound != nullptr);
    SoundSetMusic(sound);
    REQUIRE(SoundEmptyCache());
    samples = mix(12000);
    REQUIRE(SoundIsPlaying(sound));
    REQUIRE(energy(samples, 0) + energy(samples, 1) > 10);
    REQUIRE(SoundStopMusic());
    // Old backend: 2048 frames at full gain = 137.670993 per ear; front = 1/4 power.
    const double baseline = 137.670993;
    spatial.uiLoop = 0;
    spatial.Pos.flZ = 0;
    for (auto x : {-3.0f, 0.0f, 3.0f})
    {
        spatial.Pos.flX = x;
        sound = Sound3DPlay(path, &spatial);
        REQUIRE(sound != nullptr);
        samples = mix(2048);
        const auto left = energy(samples, 0), right = energy(samples, 1);
        CAPTURE(x, left, right);
        REQUIRE(near(left, baseline * (x > 0 ? 0.04 : 1)));
        REQUIRE(near(right, baseline * (x < 0 ? 0.04 : 1)));
        REQUIRE(SoundStop(sound));
    }
    spatial.Pos.flX = 0;
    spatial.Pos.flFalloffMin = 1;
    spatial.Pos.flFalloffMax = 10;
    for (auto distance : {1.0f, 100.0f})
    {
        spatial.Pos.flZ = -distance;
        sound = Sound3DPlay(path, &spatial);
        REQUIRE(sound != nullptr);
        samples = mix(2048);
        REQUIRE(near(energy(samples, 0), baseline * 0.25));
        REQUIRE(near(energy(samples, 1), baseline * 0.25));
        REQUIRE(SoundStop(sound));
    }
    spatial.Pos.flX = 3; spatial.Pos.flZ = 0;
    Sound3DSetListenerOrientation(0, 0, 1, 0, 1, 0);
    sound = Sound3DPlay(path, &spatial);
    REQUIRE(sound != nullptr);
    samples = mix(2048);
    REQUIRE((near(energy(samples, 0), baseline) && near(energy(samples, 1), baseline * 0.04)));
    Sound3DSetListenerOrientation(0, 0, -1, 0, 1, 0);
    samples = mix(2048);
    REQUIRE(energy(samples, 1) > energy(samples, 0) * 20);
    Sound3DSetListener(6, 0, 0);
    samples = mix(2048);
    REQUIRE(energy(samples, 0) > energy(samples, 1) * 20);
    REQUIRE(SoundStop(sound));
    Sound3DSetListener(0, 0, 0);
    // Velocity was normally zero at game callers; retain OpenAL-style doppler for nonzero inputs.
    spatial.Pos.flVelX = -100;
    spatial.uiLoop = 1;
    sound = Sound3DPlay(path, &spatial);
    REQUIRE(sound != nullptr);
    samples = mix(2048);
    REQUIRE((crossings(samples, 1) >= 27 && crossings(samples, 1) <= 30));
    REQUIRE(SoundStop(sound));
    spatial.Pos.flVelX = 0;
    Sound3DSetListenerVelocity(100, 0, 0);
    sound = Sound3DPlay(path, &spatial);
    REQUIRE(sound != nullptr);
    samples = mix(2048);
    REQUIRE((crossings(samples, 1) >= 25 && crossings(samples, 1) <= 28));
    REQUIRE(SoundStop(sound));
    Sound3DSetListenerVelocity(0, 0, 0);

    memset(&options, 0xff, sizeof(options));
    options.uiLoop = 1;
    options.uiVolume = 127;
    sound = SoundPlay(path, &options);
    REQUIRE(sound != nullptr);
    samples = mix(2048);
    REQUIRE((near(energy(samples, 0), baseline) && near(energy(samples, 1), baseline)));
    REQUIRE(SoundStop(sound));
    options.uiVolume = 64;
    sound = SoundPlay(path, &options);
    REQUIRE(sound != nullptr);
    samples = mix(2048);
    REQUIRE(near(energy(samples, 0), baseline * std::pow(64.0 / 127, 2)));
    REQUIRE(SoundStop(sound));
    options.uiVolume = 127;
    options.uiSpeed = 88200;
    sound = SoundPlay(path, &options);
    REQUIRE(sound != nullptr);
    samples = mix(2048);
    REQUIRE((crossings(samples) >= 40 && crossings(samples) <= 42));
    mix(1024);
    REQUIRE(!SoundIsPlaying(sound));
    SoundServiceStreams();
    options.uiSpeed = 22050;
    sound = SoundPlayStreamedFile(path, &options);
    REQUIRE(sound != nullptr);
    samples = mix(2048);
    REQUIRE((crossings(samples) >= 10 && crossings(samples) <= 11));
    mix(4000);
    REQUIRE(SoundIsPlaying(sound));
    mix(4000);
    REQUIRE(!SoundIsPlaying(sound));
    SoundServiceStreams();
    options.uiSpeed = SOUND_ERROR;
    options.uiLoop = 2;
    sound = SoundPlayStreamedFile(packed, &options);
    REQUIRE(sound != nullptr);
    mix(6000);
    REQUIRE(SoundIsPlaying(sound));
    mix(6000);
    SoundServiceStreams();
    REQUIRE(!SoundIsPlaying(sound));
    char stereo[] = "stereo.wav";
    options.uiLoop = 1;
    sound = SoundPlay(stereo, &options);
    REQUIRE(sound != nullptr);
    samples = mix(2048);
    REQUIRE(near(energy(samples, 1) / energy(samples, 0), 0.25));
    REQUIRE(SoundStop(sound));
    options.uiPan = 127;
    sound = SoundPlay(stereo, &options);
    REQUIRE(sound != nullptr);
    samples = mix(2048);
    REQUIRE((energy(samples, 0) < 0.001 && near(energy(samples, 1), baseline * 0.25)));
    REQUIRE(SoundStop(sound));
    char fallback[] = "fallback.wav";
    options.uiPan = 64;
    sound = SoundPlay(fallback, &options);
    REQUIRE(sound != nullptr);
    samples = mix(8192);
    REQUIRE(energy(samples, 0) + energy(samples, 1) > 1);
    SoundServiceStreams();
    REQUIRE(!SoundIsPlaying(sound));
    char bad[] = "bad.wav", truncated[] = "Data\\Truncated.wav";
    REQUIRE(SoundPlay(bad, &options) == nullptr);
    REQUIRE(SoundPlayStreamedFile(bad, &options) == nullptr);
    REQUIRE(SoundPlay(truncated, &options) == nullptr);
    REQUIRE(SoundPlayStreamedFile(truncated, &options) == nullptr);
    REQUIRE(SoundPlay({}, &options) == nullptr);
    REQUIRE(SoundPlayStreamedFile({}, &options) == nullptr);
    REQUIRE(!SoundRandomShouldPlay({}));
    REQUIRE(SoundStartRandom({}) == nullptr);
    sound = SoundPlayStreamedFile(fallback, &options);
    REQUIRE(sound != nullptr);
    samples = mix(8192);
    REQUIRE(energy(samples, 0) + energy(samples, 1) > 1);
    SoundServiceStreams();
    REQUIRE(!SoundIsPlaying(sound));
    RestartOnEnd restart{path, options};
    restart.options.EOSCallback = nullptr;
    options.EOSCallback = RestartOnEnd::callback;
    options.pCallbackData = &restart;
    sound = SoundPlay(path, &options);
    REQUIRE(sound != nullptr);
    mix(8192);
    SoundServiceStreams();
    REQUIRE((restart.calls == 1 && restart.restarted != nullptr && SoundIsPlaying(restart.restarted)));
    SoundServiceStreams();
    REQUIRE(restart.calls == 1);
    REQUIRE(SoundStop(restart.restarted));
    options.EOSCallback = nullptr;

    REQUIRE(SoundEmptyCache());
    {
        REQUIRE(w8_native::movie_audio_available());
        w8_native::MovieAudio movie(44100, 1);
        movie.start();
        samples = mix(1024);
        REQUIRE(energy(samples, 0) + energy(samples, 1) == 0);
        std::vector<float> pcm(4096);
        for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = float(std::sin(i * 2 * 3.14159265 * 440 / 44100) * 0.25);
        movie.append(pcm.data(), pcm.size());
        samples = mix(2048);
        REQUIRE(energy(samples, 0) + energy(samples, 1) > 1);
        mix(8192);
        movie.append(pcm.data(), pcm.size());
        movie.finish();
        REQUIRE(!movie.drained());
        mix(8192);
        REQUIRE(movie.drained());
        // No second mixer when soundman is reset while a movie is still alive.
        const auto* sharedMixer = w8_native::audio_mixer();
        ShutdownSoundManager();
        REQUIRE(InitializeSoundManager());
        REQUIRE(sharedMixer == w8_native::audio_mixer());
    }
    {
        w8_native::MovieAudio movie(48000, 2);
        std::vector<float> pcm(4800 * 2, 0.1f);
        movie.append(pcm.data(), pcm.size() / 2);
        movie.start();
        samples = mix(1024);
        REQUIRE((energy(samples, 0) > 1 && energy(samples, 1) > 1));
        movie.finish();
        mix(8192);
        REQUIRE(movie.drained());
        bool rejected = false;
        try { movie.append(pcm.data(), pcm.size() / 2); }
        catch (const std::exception&) { rejected = true; }
        REQUIRE(rejected);
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
    REQUIRE(energy(samples, 0) + energy(samples, 1) == 0);
    unsigned before_shutdown = callbacks;
    options.EOSCallback = ended;
    options.pCallbackData = &callback_data;
    REQUIRE(SoundPlay(path, &options) != nullptr);
    ShutdownSoundManager();
    REQUIRE(callbacks == before_shutdown);
    ShutdownSoundManager();
    REQUIRE(InitializeSoundManager());
    ShutdownSoundManager();

}

TEST_CASE_METHOD(AudioFixture, "Audio output device", "[.manual][audio]")
{

    char path[] = "TONE.WAV";
    SOUNDPARMS options;
    SoundPlayback sound;
        w8_native::audio_offline_for_test(false);
        REQUIRE(InitializeSoundManager());
        REQUIRE(w8_native::audio_mixer());
        memset(&options, 0xff, sizeof(options));
        options.uiLoop = 1;
        sound = SoundPlay(path, &options);
        REQUIRE(sound != nullptr);
        SDL_Delay(250);
        SoundServiceStreams();
        REQUIRE(!SoundIsPlaying(sound));
        ShutdownSoundManager();

    puts("ok: native output device opened and completed playback");
}

TEST_CASE_METHOD(AudioFixture, "Audio clips outlive cache entries without slot reuse", "[audio][ownership]")
{
    startOfflineAudio();
    std::vector<SoundClip> clips;
    for (unsigned i = 0; i < 140; ++i) {
        auto name = "clip" + std::to_string(i) + ".wav";
        write(asset / name, wave);
        auto clip = SoundLoadSample(name.data());
        REQUIRE(clip);
        REQUIRE(SoundLoadSample(name.data()) == clip);
        clips.push_back(std::move(clip));
    }
    std::weak_ptr<AudioClip> retained = clips.front();
    REQUIRE(SoundEmptyCache());
    REQUIRE_FALSE(retained.expired());
    clips.clear();
    REQUIRE(retained.expired());

    char path[] = "tone.wav";
    RANDOMPARMS random;
    memset(&random, 0xff, sizeof(random));
    random.uiTimeMin = random.uiTimeMax = 0;
    auto clip = SoundPlayRandom(path, &random);
    REQUIRE(clip);
    REQUIRE(SoundEmptyCache());
    // A retained emitter can still play its clip after the cache releases it.
    const auto sound = SoundStartRandom(clip);
    REQUIRE(sound != nullptr);
    retained = clip;
    clip.reset();
    REQUIRE_FALSE(retained.expired());
    REQUIRE(SoundStop(sound));
    REQUIRE(retained.expired());
}

TEST_CASE_METHOD(AudioFixture, "Owned clips retain the mixer across sound shutdown", "[audio][ownership]")
{
    startOfflineAudio();
    char path[] = "tone.wav";
    auto clip = SoundLoadSample(path);
    REQUIRE(clip);
    std::weak_ptr<AudioClip> lifetime = clip;
    ShutdownSoundManager();
    REQUIRE_FALSE(lifetime.expired());
    REQUIRE(InitializeSoundManager());
    clip.reset();
    REQUIRE(lifetime.expired());
}

TEST_CASE_METHOD(AudioFixture, "Playback references never alias replacement tracks", "[audio][ownership]")
{
    startOfflineAudio();
    char path[] = "tone.wav";
    SOUNDPARMS options;
    memset(&options, 0xff, sizeof(options));
    options.uiLoop = 0;
    auto first = SoundPlay(path, &options);
    REQUIRE(first);
    REQUIRE(SoundStop(first));
    auto second = SoundPlay(path, &options);
    REQUIRE(second);
    REQUIRE(first != second);
    REQUIRE_FALSE(SoundStop(first));
    REQUIRE_FALSE(SoundSetVolume(first, 0));
    REQUIRE(SoundGetVolume(second) == 127);
    REQUIRE(SoundIsPlaying(second));
    ShutdownSoundManager();
    REQUIRE_FALSE(SoundIsPlaying(second));
    REQUIRE(InitializeSoundManager());
    auto third = SoundPlay(path, &options);
    REQUIRE(third);
    REQUIRE_FALSE(SoundStop(second));
    REQUIRE(SoundIsPlaying(third));
}

TEST_CASE_METHOD(AudioFixture, "Mixer service owns discarded playback until completion", "[audio][ownership]")
{
    startOfflineAudio();
    char path[] = "tone.wav";
    std::weak_ptr<AudioPlayback> lifetime;
    {
        auto playback = SoundPlay(path, nullptr);
        REQUIRE(playback);
        lifetime = playback;
    }
    REQUIRE_FALSE(lifetime.expired());
    const auto samples = mix(8192);
    REQUIRE(energy(samples, 0) + energy(samples, 1) > 10);
    SoundServiceStreams();
    REQUIRE(lifetime.expired());
}

TEST_CASE_METHOD(AudioFixture, "Playback completion permits stop and shutdown reentry", "[audio][ownership]")
{
    startOfflineAudio();
    struct Context {
        SoundPlayback other;
        unsigned calls = 0;
    } context;
    char path[] = "tone.wav";
    SOUNDPARMS options;
    memset(&options, 0xff, sizeof(options));
    options.EOSCallback = [](void* data) {
        auto& context = *static_cast<Context*>(data);
        ++context.calls;
        SoundStop(context.other);
        ShutdownSoundManager();
    };
    options.pCallbackData = &context;
    auto first = SoundPlay(path, &options);
    REQUIRE(first);
    options.EOSCallback = nullptr;
    options.uiLoop = 0;
    context.other = SoundPlayStreamedFile(path, &options);
    REQUIRE(context.other);
    mix(8192);
    SoundServiceStreams();
    REQUIRE(context.calls == 1);
    REQUIRE_FALSE(SoundIsPlaying(first));
    REQUIRE_FALSE(SoundIsPlaying(context.other));
    SoundServiceStreams();
    REQUIRE(context.calls == 1);
}

TEST_CASE_METHOD(AudioFixture, "Mixer tags stop only the original group before reentrant callbacks", "[audio][ownership]")
{
    startOfflineAudio();
    struct Context {
        SOUNDPARMS options;
        SoundPlayback replacement;
        unsigned calls = 0;
    } context;
    memset(&context.options, 0xff, sizeof(context.options));
    context.options.uiLoop = 0;
    context.options.uiPriority = 42;
    context.options.EOSCallback = [](void* data) {
        auto& context = *static_cast<Context*>(data);
        ++context.calls;
        auto options = context.options;
        options.EOSCallback = nullptr;
        context.replacement = SoundPlayStreamedFile("tone.wav", &options);
    };
    context.options.pCallbackData = &context;
    auto first = SoundPlay("tone.wav", &context.options);
    REQUIRE(first);
    auto options = context.options;
    options.EOSCallback = nullptr;
    auto second = SoundPlayStreamedFile("tone.wav", &options);
    REQUIRE(second);
    options.uiPriority = 43;
    auto unrelated = SoundPlay("tone.wav", &options);
    REQUIRE(unrelated);
    auto music = SoundPlay("tone.wav", &options);
    REQUIRE(music);
    SoundSetMusic(music);
    REQUIRE_FALSE(SoundStopGroup(123));
    REQUIRE(SoundStopGroup(42));
    REQUIRE(context.calls == 1);
    REQUIRE_FALSE(SoundIsPlaying(first));
    REQUIRE_FALSE(SoundIsPlaying(second));
    REQUIRE(SoundIsPlaying(context.replacement));
    REQUIRE(SoundIsPlaying(unrelated));
    REQUIRE(SoundIsPlaying(music));
    REQUIRE(SoundEmptyCache());
    REQUIRE_FALSE(SoundIsPlaying(context.replacement));
    REQUIRE_FALSE(SoundIsPlaying(unrelated));
    REQUIRE(SoundIsPlaying(music));
    REQUIRE(SoundStopMusic());
    REQUIRE_FALSE(SoundStopMusic());
    REQUIRE(context.calls == 1);
}

TEST_CASE_METHOD(AudioFixture, "Mixer completion defers game callbacks until service or next play", "[audio][ownership]")
{
    startOfflineAudio();
    SOUNDPARMS options;
    memset(&options, 0xff, sizeof(options));
    unsigned callback_data = 0;
    options.EOSCallback = ended;
    options.pCallbackData = &callback_data;
    auto first = SoundPlayStreamedFile("tone.wav", &options);
    REQUIRE(first);
    mix(8192);
    REQUIRE_FALSE(SoundIsPlaying(first));
    REQUIRE(callback_data == 0);
    options.EOSCallback = nullptr;
    auto next = SoundPlay("tone.wav", &options);
    REQUIRE(next);
    REQUIRE(callback_data == 1);
    SoundServiceStreams();
    REQUIRE(callback_data == 1);
}
