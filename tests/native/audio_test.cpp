#include <catch2/catch_test_macros.hpp>
#include <ios>
#include <wiz8/native_audio.h>
#include <wiz8/filesystem.h>
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

/* Native sound service -> SDL3_mixer, with deterministic offline mixing. */
#include "native/audio_test.h"
#include "wiz8/slf.h"
#include "wiz8/local_code/CombatSound.h"
#include "wiz8/voice_line.h"
#include "native/movie_audio.h"
#include "soundman.h"
#include "temporary_directory.h"
#include "scoped_asset_roots.h"
#include "catch2/catch_message.hpp"
#include "wiz8/audio_resources.h"
#include <glm/ext/vector_float3.hpp>

static std::uint32_t callbacks = 0;
static void word(std::vector<std::uint8_t>& data, std::uint32_t value, std::int32_t bytes)
{
    for (std::int32_t i = 0; i < bytes; ++i)
        data.push_back(value >> (i * 8));
}
static void tag(std::vector<std::uint8_t>& data, const char* value)
{
    data.insert(data.end(), value, value + 4);
}
static std::vector<std::uint8_t> wav(bool stereo = false)
{
    std::vector<std::uint8_t> bytes;
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
    for (std::int32_t i = 0; i < 4410; ++i)
    {
        word(bytes, std::uint32_t(std::int16_t(std::sin(i * 2 * 3.14159265 * 440 / 44100) * 12000)), 2);
        if (stereo)
            word(bytes, std::uint32_t(std::int16_t(std::sin(i * 2 * 3.14159265 * 880 / 44100) * 6000)), 2);
    }
    return bytes;
}
static void write(const std::filesystem::path& name, const std::vector<std::uint8_t>& bytes)
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
static double energy(const std::vector<float>& values, std::int32_t channel)
{
    double sum = 0;
    for (size_t i = channel; i < values.size(); i += 2)
        sum += values[i] * values[i];
    return sum;
}
static std::uint32_t crossings(const std::vector<float>& samples, std::int32_t channel = 0)
{
    std::uint32_t count = 0;
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
    std::vector<std::uint8_t> wave = wav();
    ScopedAssetRoots asset_roots;

    AudioFixture()
    {
        callbacks = 0;
        std::filesystem::create_directories(asset);
        std::filesystem::create_directories(user);
        asset_roots.configure({asset.string(), user.string(), {}});
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
        std::vector<std::uint8_t> archive(reinterpret_cast<std::uint8_t*>(&header),
                                           reinterpret_cast<std::uint8_t*>(&header) + sizeof(header));
        archive.insert(archive.end(), wave.begin(), wave.end());
        archive.insert(archive.end(), reinterpret_cast<std::uint8_t*>(entries),
                       reinterpret_cast<std::uint8_t*>(entries) + sizeof(entries));
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
            std::uint8_t bytes[64]{};
            REQUIRE(first->read(bytes, sizeof(bytes)).bytes == sizeof(bytes));
            REQUIRE(std::memcmp(bytes, wave.data(), sizeof(bytes)) == 0);
            REQUIRE((second->tell() == 0 && loose->tell() == 0));
            REQUIRE(second->read(bytes, 32).bytes == 32);
            REQUIRE(std::memcmp(bytes, wave.data(), 32) == 0);
            first.reset();
            REQUIRE(second->seek(wave.size() - 32, wiz8::SeekOrigin::begin) ==
                  std::int64_t(wave.size() - 32));
            REQUIRE(second->read(bytes, 32).bytes == 32);
            REQUIRE(std::memcmp(bytes, wave.data() + wave.size() - 32, 32) == 0);
            REQUIRE(loose->read(bytes, sizeof(bytes)).bytes == sizeof(bytes));
            REQUIRE(std::memcmp(bytes, wave.data(), sizeof(bytes)) == 0);
        }
    }

    void startOfflineAudio()
    {
        w8_native::audio_offline_for_test(true);
        REQUIRE(wiz8::sound::initialize(true));
    }

    ~AudioFixture()
    {
        wiz8::sound::shutdown();
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
    for (std::int32_t repeat = 0; repeat < 2; ++repeat) {
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
    using wiz8::sound::Group;
    using wiz8::sound::PlayOptions;
    startOfflineAudio();
    std::uint32_t callback_data = 0;
    const auto count_end = [&] { ++callbacks; ++callback_data; };
    const std::string_view path = "TONE.WAV";
    auto sound = wiz8::sound::play(path, {.volume = 127, .on_finished = count_end});
    REQUIRE((sound != nullptr && wiz8::sound::is_playing(sound)));
    REQUIRE(wiz8::sound::is_file_playing(path));
    auto position = wiz8::sound::playback_time(sound);
    REQUIRE(position.has_value());
    REQUIRE(position->total_ms == 100);
    auto samples = mix(2048);
    REQUIRE((energy(samples, 0) > 10 && energy(samples, 1) > 10));
    position = wiz8::sound::playback_time(sound);
    REQUIRE(position.has_value());
    REQUIRE((position->current_ms > 0 && position->current_ms <= 100));
    REQUIRE(wiz8::sound::volume(sound) == 127u);
    REQUIRE(wiz8::sound::set_volume(sound, 64));
    REQUIRE(wiz8::sound::volume(sound) == 64u);
    mix(8192);
    REQUIRE(!wiz8::sound::is_playing(sound));
    wiz8::sound::service();
    REQUIRE((callbacks == 1 && callback_data == 1));
    REQUIRE(!wiz8::sound::volume(sound));
    wiz8::sound::service();
    REQUIRE(callbacks == 1);
    sound = wiz8::sound::play(path, {.loops = 2});
    REQUIRE(sound != nullptr);
    mix(6000);
    REQUIRE(wiz8::sound::is_playing(sound));
    mix(6000);
    wiz8::sound::service();
    REQUIRE(!wiz8::sound::is_playing(sound));
    sound = wiz8::sound::stream(path, {.volume = 127, .loops = 0, .group = Group::music});
    REQUIRE(sound != nullptr);
    mix(12000);
    REQUIRE(wiz8::sound::is_playing(sound));
    REQUIRE(wiz8::sound::stop_group(Group::music));
    REQUIRE(!wiz8::sound::is_playing(sound));
    // A fading stop keeps the playback audible until the mixer completes the fade.
    sound = wiz8::sound::play(path, {.volume = 127, .loops = 0});
    REQUIRE(sound != nullptr);
    REQUIRE(wiz8::sound::stop(sound, 20));
    REQUIRE(wiz8::sound::is_playing(sound));
    mix(4096);
    wiz8::sound::service();
    REQUIRE(!wiz8::sound::is_playing(sound));
    REQUIRE(!wiz8::sound::stop(sound, 20));
    auto first = wiz8::sound::play(path, {.loops = 0, .group = Group::ambient});
    auto second = wiz8::sound::stream(path, {.loops = 0, .group = Group::ambient});
    auto effect = wiz8::sound::play(path, {.loops = 0});
    REQUIRE((first != nullptr && second != nullptr && effect != nullptr));
    REQUIRE(wiz8::sound::stop_group(Group::ambient));
    REQUIRE((!wiz8::sound::is_playing(first) && !wiz8::sound::is_playing(second)));
    REQUIRE(wiz8::sound::is_playing(effect));
    REQUIRE(wiz8::sound::stop(effect));
    sound = wiz8::sound::play(path, {.volume = 127, .position = glm::vec3(0, 0, -3)});
    REQUIRE((sound != nullptr && wiz8::sound::is_playing(sound)));
    samples = mix(1024);
    REQUIRE(energy(samples, 0) + energy(samples, 1) > 1);
    wiz8::sound::set_position(sound, glm::vec3(1, 0, -3));
    REQUIRE(wiz8::sound::stop(sound));
    wiz8::sound::release_cache();
    const std::string_view packed = "Data\\Packed.wav";
    sound = wiz8::sound::stream(packed);
    REQUIRE(sound != nullptr);
    position = wiz8::sound::playback_time(sound);
    REQUIRE((position.has_value() && position->total_ms == 100));
    samples = mix(8192);
    REQUIRE(energy(samples, 0) + energy(samples, 1) > 10);
    wiz8::sound::service();
    REQUIRE(!wiz8::sound::is_playing(sound));
    for (std::int32_t i = 0; i < 64; ++i)
        REQUIRE(wiz8::sound::play(path, {.loops = 0, .group = Group::ambient}) != nullptr);
    REQUIRE(wiz8::sound::stop_group(Group::ambient));
    sound = wiz8::sound::play(path, {.loops = 0, .group = Group::music});
    REQUIRE(sound != nullptr);
    wiz8::sound::release_cache();
    samples = mix(12000);
    REQUIRE(wiz8::sound::is_playing(sound));
    REQUIRE(energy(samples, 0) + energy(samples, 1) > 10);
    REQUIRE(wiz8::sound::stop_group(Group::music));
    // Old backend: 2048 frames at full gain = 137.670993 per ear; front = 1/4 power.
    const double baseline = 137.670993;
    for (auto x : {-3.0f, 0.0f, 3.0f})
    {
        sound = wiz8::sound::play(path, {.volume = 127, .loops = 0, .position = glm::vec3(x, 0, 0)});
        REQUIRE(sound != nullptr);
        samples = mix(2048);
        const auto left = energy(samples, 0), right = energy(samples, 1);
        CAPTURE(x, left, right);
        REQUIRE(near(left, baseline * (x > 0 ? 0.04 : 1)));
        REQUIRE(near(right, baseline * (x < 0 ? 0.04 : 1)));
        REQUIRE(wiz8::sound::stop(sound));
    }
    // Callers apply distance falloff; the service only pans.
    for (auto distance : {1.0f, 100.0f})
    {
        sound = wiz8::sound::play(path, {.volume = 127, .loops = 0, .position = glm::vec3(0, 0, -distance)});
        REQUIRE(sound != nullptr);
        samples = mix(2048);
        REQUIRE(near(energy(samples, 0), baseline * 0.25));
        REQUIRE(near(energy(samples, 1), baseline * 0.25));
        REQUIRE(wiz8::sound::stop(sound));
    }
    sound = wiz8::sound::play(path, {.volume = 127, .loops = 0, .position = glm::vec3(3, 0, 0)});
    REQUIRE(sound != nullptr);
    mix(1024);
    wiz8::sound::set_position(sound, glm::vec3(-3, 0, 0));
    samples = mix(2048);
    REQUIRE(energy(samples, 0) > energy(samples, 1) * 20);
    REQUIRE(wiz8::sound::stop(sound));
    sound = wiz8::sound::play(path, {.volume = 127});
    REQUIRE(sound != nullptr);
    samples = mix(2048);
    REQUIRE((near(energy(samples, 0), baseline) && near(energy(samples, 1), baseline)));
    REQUIRE(wiz8::sound::stop(sound));
    sound = wiz8::sound::play(path, {.volume = 64});
    REQUIRE(sound != nullptr);
    samples = mix(2048);
    REQUIRE(near(energy(samples, 0), baseline * std::pow(64.0 / 127, 2)));
    REQUIRE(wiz8::sound::stop(sound));
    wiz8::sound::set_default_volume(64);
    sound = wiz8::sound::play(path);
    REQUIRE(wiz8::sound::volume(sound) == 64u);
    REQUIRE(wiz8::sound::stop(sound));
    wiz8::sound::set_default_volume(127);
    sound = wiz8::sound::play(path, {.volume = 127, .fade_in_ms = 1000});
    REQUIRE(sound != nullptr);
    samples = mix(2048);
    REQUIRE(energy(samples, 0) < baseline * 0.05);
    REQUIRE(wiz8::sound::stop(sound));
    sound = wiz8::sound::play(path, {.volume = 127, .rate = 88200});
    REQUIRE(sound != nullptr);
    samples = mix(2048);
    REQUIRE((crossings(samples) >= 40 && crossings(samples) <= 42));
    mix(1024);
    REQUIRE(!wiz8::sound::is_playing(sound));
    wiz8::sound::service();
    sound = wiz8::sound::stream(path, {.volume = 127, .rate = 22050});
    REQUIRE(sound != nullptr);
    samples = mix(2048);
    REQUIRE((crossings(samples) >= 10 && crossings(samples) <= 11));
    mix(4000);
    REQUIRE(wiz8::sound::is_playing(sound));
    mix(4000);
    REQUIRE(!wiz8::sound::is_playing(sound));
    wiz8::sound::service();
    sound = wiz8::sound::stream(packed, {.volume = 127, .loops = 2});
    REQUIRE(sound != nullptr);
    mix(6000);
    REQUIRE(wiz8::sound::is_playing(sound));
    mix(6000);
    wiz8::sound::service();
    REQUIRE(!wiz8::sound::is_playing(sound));
    const std::string_view stereo = "stereo.wav";
    sound = wiz8::sound::play(stereo, {.volume = 127});
    REQUIRE(sound != nullptr);
    samples = mix(2048);
    REQUIRE(near(energy(samples, 1) / energy(samples, 0), 0.25));
    REQUIRE(wiz8::sound::stop(sound));
    const std::string_view fallback = "fallback.wav";
    sound = wiz8::sound::play(fallback, {.volume = 127});
    REQUIRE(sound != nullptr);
    samples = mix(8192);
    REQUIRE(energy(samples, 0) + energy(samples, 1) > 1);
    wiz8::sound::service();
    REQUIRE(!wiz8::sound::is_playing(sound));
    for (const std::string_view rejected : {"bad.wav", "Data\\Truncated.wav", ""})
    {
        REQUIRE(wiz8::sound::play(rejected) == nullptr);
        REQUIRE(wiz8::sound::stream(rejected) == nullptr);
        REQUIRE(wiz8::sound::load(rejected) == nullptr);
    }
    REQUIRE(wiz8::sound::play(wiz8::sound::Clip{}) == nullptr);
    sound = wiz8::sound::stream(fallback, {.volume = 127});
    REQUIRE(sound != nullptr);
    samples = mix(8192);
    REQUIRE(energy(samples, 0) + energy(samples, 1) > 1);
    wiz8::sound::service();
    REQUIRE(!wiz8::sound::is_playing(sound));
    std::uint32_t restarts = 0;
    wiz8::sound::Playback restarted;
    sound = wiz8::sound::play(path, {.volume = 127, .on_finished = [&] {
        ++restarts;
        restarted = wiz8::sound::play(path, {.volume = 127});
    }});
    REQUIRE(sound != nullptr);
    mix(8192);
    wiz8::sound::service();
    REQUIRE((restarts == 1 && restarted != nullptr && wiz8::sound::is_playing(restarted)));
    wiz8::sound::service();
    REQUIRE(restarts == 1);
    REQUIRE(wiz8::sound::stop(restarted));
    wiz8::sound::release_cache();
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
        wiz8::sound::shutdown();
        REQUIRE(wiz8::sound::initialize(true));
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
    std::uint32_t before_shutdown = callbacks;
    REQUIRE(wiz8::sound::play(path, {.volume = 127, .on_finished = count_end}) != nullptr);
    wiz8::sound::shutdown();
    REQUIRE(callbacks == before_shutdown);
    wiz8::sound::shutdown();
    REQUIRE(wiz8::sound::initialize(true));
    wiz8::sound::shutdown();

}

TEST_CASE_METHOD(AudioFixture, "Audio output device", "[.manual][audio]")
{
    w8_native::audio_offline_for_test(false);
    REQUIRE(wiz8::sound::initialize(true));
    REQUIRE(w8_native::audio_mixer());
    const auto sound = wiz8::sound::play("TONE.WAV");
    REQUIRE(sound != nullptr);
    SDL_Delay(250);
    wiz8::sound::service();
    REQUIRE(!wiz8::sound::is_playing(sound));
    wiz8::sound::shutdown();
    puts("ok: native output device opened and completed playback");
}

TEST_CASE_METHOD(AudioFixture, "Audio clips outlive cache entries without slot reuse", "[audio][ownership]")
{
    startOfflineAudio();
    std::vector<wiz8::sound::Clip> clips;
    for (std::uint32_t i = 0; i < 140; ++i) {
        auto name = "clip" + std::to_string(i) + ".wav";
        write(asset / name, wave);
        auto clip = wiz8::sound::load(name);
        REQUIRE(clip);
        REQUIRE(wiz8::sound::load(name) == clip);
        clips.push_back(std::move(clip));
    }
    std::weak_ptr<wiz8::sound::AudioClip> retained = clips.front();
    wiz8::sound::release_cache();
    REQUIRE_FALSE(retained.expired());
    clips.clear();
    REQUIRE(retained.expired());
    auto clip = wiz8::sound::load("tone.wav");
    REQUIRE(clip);
    wiz8::sound::release_cache();
    // A retained emitter can still play its clip after the cache releases it.
    const auto sound = wiz8::sound::play(clip, {.group = wiz8::sound::Group::ambient});
    REQUIRE(sound != nullptr);
    retained = clip;
    clip.reset();
    REQUIRE_FALSE(retained.expired());
    REQUIRE(wiz8::sound::stop(sound));
    REQUIRE(retained.expired());
}

TEST_CASE_METHOD(AudioFixture, "Owned clips retain the mixer across sound shutdown", "[audio][ownership]")
{
    startOfflineAudio();
    auto clip = wiz8::sound::load("tone.wav");
    REQUIRE(clip);
    std::weak_ptr<wiz8::sound::AudioClip> lifetime = clip;
    wiz8::sound::shutdown();
    REQUIRE_FALSE(lifetime.expired());
    REQUIRE(wiz8::sound::initialize(true));
    clip.reset();
    REQUIRE(lifetime.expired());
}

TEST_CASE_METHOD(AudioFixture, "Playback references never alias replacement tracks", "[audio][ownership]")
{
    startOfflineAudio();
    auto first = wiz8::sound::play("tone.wav", {.loops = 0});
    REQUIRE(first);
    REQUIRE(wiz8::sound::stop(first));
    auto second = wiz8::sound::play("tone.wav", {.loops = 0});
    REQUIRE(second);
    REQUIRE(first != second);
    REQUIRE_FALSE(wiz8::sound::stop(first));
    REQUIRE_FALSE(wiz8::sound::set_volume(first, 0));
    REQUIRE(wiz8::sound::volume(second) == 127u);
    REQUIRE(wiz8::sound::is_playing(second));
    wiz8::sound::shutdown();
    REQUIRE_FALSE(wiz8::sound::is_playing(second));
    REQUIRE(wiz8::sound::initialize(true));
    auto third = wiz8::sound::play("tone.wav", {.loops = 0});
    REQUIRE(third);
    REQUIRE_FALSE(wiz8::sound::stop(second));
    REQUIRE(wiz8::sound::is_playing(third));
}

TEST_CASE_METHOD(AudioFixture, "Mixer service owns discarded playback until completion", "[audio][ownership]")
{
    startOfflineAudio();
    std::weak_ptr<wiz8::sound::AudioPlayback> lifetime;
    {
        auto playback = wiz8::sound::play("tone.wav");
        REQUIRE(playback);
        lifetime = playback;
    }
    REQUIRE_FALSE(lifetime.expired());
    const auto samples = mix(8192);
    REQUIRE(energy(samples, 0) + energy(samples, 1) > 10);
    wiz8::sound::service();
    REQUIRE(lifetime.expired());
}

TEST_CASE_METHOD(AudioFixture, "Playback completion permits stop and shutdown reentry", "[audio][ownership]")
{
    startOfflineAudio();
    wiz8::sound::Playback other;
    std::uint32_t calls = 0;
    auto first = wiz8::sound::play("tone.wav", {.on_finished = [&] {
        ++calls;
        wiz8::sound::stop(other);
        wiz8::sound::shutdown();
    }});
    REQUIRE(first);
    other = wiz8::sound::stream("tone.wav", {.loops = 0});
    REQUIRE(other);
    mix(8192);
    wiz8::sound::service();
    REQUIRE(calls == 1);
    REQUIRE_FALSE(wiz8::sound::is_playing(first));
    REQUIRE_FALSE(wiz8::sound::is_playing(other));
    wiz8::sound::service();
    REQUIRE(calls == 1);
}

TEST_CASE_METHOD(AudioFixture, "Mixer tags stop only the original group before reentrant callbacks", "[audio][ownership]")
{
    using wiz8::sound::Group;
    startOfflineAudio();
    wiz8::sound::Playback replacement;
    std::uint32_t calls = 0;
    auto first = wiz8::sound::play("tone.wav", {.loops = 0, .group = Group::ambient, .on_finished = [&] {
        ++calls;
        replacement = wiz8::sound::stream("tone.wav", {.loops = 0, .group = Group::ambient});
    }});
    REQUIRE(first);
    auto second = wiz8::sound::stream("tone.wav", {.loops = 0, .group = Group::ambient});
    REQUIRE(second);
    auto unrelated = wiz8::sound::play("tone.wav", {.loops = 0});
    REQUIRE(unrelated);
    auto music = wiz8::sound::play("tone.wav", {.loops = 0, .group = Group::music});
    REQUIRE(music);
    REQUIRE(wiz8::sound::stop_group(Group::ambient));
    REQUIRE(calls == 1);
    REQUIRE_FALSE(wiz8::sound::is_playing(first));
    REQUIRE_FALSE(wiz8::sound::is_playing(second));
    REQUIRE(wiz8::sound::is_playing(replacement));
    REQUIRE(wiz8::sound::is_playing(unrelated));
    REQUIRE(wiz8::sound::is_playing(music));
    wiz8::sound::release_cache();
    REQUIRE_FALSE(wiz8::sound::is_playing(replacement));
    REQUIRE_FALSE(wiz8::sound::is_playing(unrelated));
    REQUIRE(wiz8::sound::is_playing(music));
    REQUIRE(wiz8::sound::stop_group(Group::music));
    REQUIRE_FALSE(wiz8::sound::stop_group(Group::music));
    REQUIRE(calls == 1);
}

TEST_CASE_METHOD(AudioFixture, "Stopping completed playback drains its deferred callback", "[audio][ownership]")
{
    startOfflineAudio();
    auto callback_data = std::make_unique<std::uint32_t>(0);
    auto sound = wiz8::sound::stream("tone.wav", {.on_finished = [data = callback_data.get()] {
        ++callbacks;
        ++*data;
    }});
    REQUIRE(sound);
    mix(8192);
    REQUIRE_FALSE(wiz8::sound::is_playing(sound));
    REQUIRE(*callback_data == 0);
    REQUIRE_FALSE(wiz8::sound::stop(sound));
    REQUIRE(*callback_data == 1);
    callback_data.reset();
    REQUIRE_FALSE(wiz8::sound::stop(sound));
    wiz8::sound::service();
    wiz8::sound::release_cache();
    REQUIRE(callbacks == 1);
}

TEST_CASE_METHOD(AudioFixture, "Destroying a voice line cancels its deferred completion", "[audio][ownership]")
{
    // Character events complete when their voice ends; a voice line destroyed
    // with its completion still pending must not call back into the dead event.
    startOfflineAudio();
    auto line = std::make_unique<W8VoiceLine>();
    std::uint32_t calls = 0;
    REQUIRE(line->Play("tone.wav", 127, [&] { ++calls; }));
    mix(8192);
    REQUIRE_FALSE(wiz8::sound::is_playing(line->playback()));
    line.reset();
    wiz8::sound::service();
    wiz8::sound::release_cache();
    REQUIRE(calls == 0);
}

TEST_CASE_METHOD(AudioFixture, "Mixer completion defers game callbacks until service or next play", "[audio][ownership]")
{
    startOfflineAudio();
    std::uint32_t calls = 0;
    auto first = wiz8::sound::stream("tone.wav", {.on_finished = [&] { ++calls; }});
    REQUIRE(first);
    mix(8192);
    REQUIRE_FALSE(wiz8::sound::is_playing(first));
    REQUIRE(calls == 0);
    auto next = wiz8::sound::play("tone.wav");
    REQUIRE(next);
    REQUIRE(calls == 1);
    wiz8::sound::service();
    REQUIRE(calls == 1);
}
