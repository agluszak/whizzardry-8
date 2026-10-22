#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <catch2/catch_test_macros.hpp>
#include <wiz8/audio/native_audio.h>
#include <wiz8/filesystem.h>

#include "asset_paths.h"
#include "prepared_assets.h"
#include "scoped_game.h"
#include "wiz8/checked_integer.h"

/* Native sound service -> SDL3_mixer, with deterministic offline mixing. */
#include <glm/ext/vector_float3.hpp>

#include "catch2/catch_message.hpp"
#include "scoped_asset_roots.h"
#include "slf.h"
#include "temporary_directory.h"
#include "test_worlds.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/audio/audio_test.h"
#include "wiz8/audio/movie_audio.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/audio_resources.h"
#include "wiz8/button_interaction.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/combat/sounds.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/platform/input.h"
#include "wiz8/platform/random.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/world/world.h"

static unsigned callbacks = 0;
static void word(std::vector<unsigned char>& data, unsigned value, int bytes)
{
    for (int i = 0; i < bytes; ++i) data.push_back(value >> (i * 8));
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
    for (int i = 0; i < 4410; ++i) {
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
    for (size_t i = channel; i < values.size(); i += 2) sum += values[i] * values[i];
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

struct AudioFixture {
    ScopedGame game;
    ScopedTemporaryDirectory directory{"wiz8-audio"};
    std::filesystem::path temporary = directory.path;
    std::filesystem::path asset = temporary / "assets";
    std::filesystem::path user = temporary / "user";
    std::vector<unsigned char> wave = wav();
    ScopedAssetRoots asset_roots;

    AudioFixture()
    {
        callbacks = 0;
        std::filesystem::create_directories(asset);
        std::filesystem::create_directories(user);
        asset_roots.configure({asset.string(), user.string()});
        std::filesystem::copy_file(WIZ8_AUDIO_TEST_MP3, asset / "fallback.mp3");
        write(asset / "tone.wav", wave);
        write(asset / "stereo.wav", wav(true));
        write(asset / "bad.wav", {'n', 'o', 't', 'a', 'w', 'a', 'v'});
        std::filesystem::create_directories(asset / "data");
        wiz8::SlfHeader header{};
        strcpy(header.sLibName, "data.slf");
        strcpy(header.sPathToLibrary, "Data\\");
        header.iEntries = header.iUsed = 2;
        header.iVersion = 0x200;
        wiz8::SlfEntry entries[2]{};
        strcpy(entries[0].sFileName, "packed.wav");
        entries[0].uiOffset = sizeof(header);
        entries[0].uiLength = wiz8::checked_integer<std::uint32_t>(wave.size());
        strcpy(entries[1].sFileName, "truncated.wav");
        entries[1].uiOffset = sizeof(header);
        entries[1].uiLength = 16;
        std::vector<unsigned char> archive(reinterpret_cast<unsigned char*>(&header),
                                           reinterpret_cast<unsigned char*>(&header) +
                                               sizeof(header));
        archive.insert(archive.end(), wave.begin(), wave.end());
        archive.insert(archive.end(), reinterpret_cast<unsigned char*>(entries),
                       reinterpret_cast<unsigned char*>(entries) + sizeof(entries));
        write(asset / "data" / "data.slf", archive);

        extract_test_archive("data\\data.slf");
    }

    void checkArchiveStreams()
    {
        {
            char packed[] = "data\\packed.wav";
            auto entry = [&]() {
                try {
                    return wiz8::open_file(wiz8::asset_path(packed), wiz8::OpenMode::read);
                } catch (const std::exception&) {
                    return std::unique_ptr<wiz8::File>{};
                }
            }();
            REQUIRE(entry);
            auto first = wiz8::open_file(wiz8::asset_path(packed)),
                 second = wiz8::open_file(wiz8::asset_path(packed));
            entry.reset();
            REQUIRE((first && second && first->tell() == 0 && second->tell() == 0));
            auto loose = wiz8::open_file(wiz8::asset_path("tone.wav"));
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
        REQUIRE(wiz8::sound::initialize(true));
    }

    ~AudioFixture() { wiz8::sound::shutdown(); }
};

TEST_CASE_METHOD(AudioFixture, "Ambient commands operate on their supplied world",
                 "[audio][ambient]")
{
    startOfflineAudio();
    game.game.level.replacePrimary(MakeWorld());
    auto& active = *GetWorld();
    StandaloneWorld standalone_candidate;
    World& candidate = standalone_candidate.world;
    placeParty(active, glm::vec3(1000));
    placeParty(candidate, glm::vec3(0));
    for (auto* world : {&active, &candidate}) {
        auto sound = std::make_unique<W8AmbientSound>();
        sound->name = "shared-name";
        sound->radius = 100;
        sound->stopped = 1;
        world->audio.emitters.push_back(std::move(sound));
    }
    auto& sound = *candidate.audio.emitters.front();
    PositionAmbientSoundByName(candidate, "shared-name");
    REQUIRE(sound.stopped == 0);
    REQUIRE(sound.in_range == 1);
    REQUIRE(active.audio.emitters.front()->stopped == 1);
    StopAmbientSoundByName(candidate, "shared-name");
    REQUIRE(sound.stopped == 1);
    ToggleAmbientSoundByName(candidate, "shared-name");
    REQUIRE(sound.stopped == 0);
    REQUIRE(sound.in_range == 1);
    REQUIRE(active.audio.emitters.front()->stopped == 1);
}

TEST_CASE_METHOD(AudioFixture, "Ambient rows do not inherit a previous row's mode",
                 "[audio][ambient]")
{
    startOfflineAudio();
    StandaloneWorld standalone_candidate;
    World& candidate = standalone_candidate.world;
    // The first row uses random replay; the malformed second row must not inherit it.
    std::ofstream(asset / "ambience.scf") << ".\n"
                                          << "tone.wav -1 -1 0 127 10 20 -1\n"
                                          << "tone.wav -1 -1 0 127 10 20\n";
    REQUIRE_FALSE(ReadAmbientSoundListFile(candidate, "ambience.scf"));
    REQUIRE(candidate.audio.tracks.size() == 1);
    REQUIRE_FALSE(candidate.audio.tracks.front()->loops);
    std::ofstream(asset / "ambience.scf") << ".\n" << "tone.wav -1 -1 -1 -1 -1 -1 0\n";
    StandaloneWorld standalone_direct;
    World& direct = standalone_direct.world;
    REQUIRE(ReadAmbientSoundListFile(direct, "ambience.scf"));
    REQUIRE(direct.audio.tracks.size() == 1);
    REQUIRE(direct.audio.tracks.front()->loops == 0);
    REQUIRE(direct.audio.tracks.front()->volume_max == 127);
}

TEST_CASE_METHOD(AudioFixture, "Prepared audio files, tracks and combat sounds", "[audio][streams]")
{
    checkArchiveStreams();
    startOfflineAudio();
    // Variant selection accepts a bounded, read-only name and does not append
    // into its backing storage or consume the characters beyond the view.
    const auto combat = asset / "data" / "sound" / "combat";
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
        auto looseInput = w8_native::open_audio_input("tone.wav");
        auto packedInput = w8_native::open_audio_input("data\\packed.wav");
        auto loopInput = w8_native::open_audio_input("data\\packed.wav");
        auto badInput = w8_native::open_audio_input("data\\truncated.wav");
        REQUIRE((looseInput && packedInput && loopInput && badInput));
        REQUIRE(SDL_GetIOSize(packedInput.get()) == Sint64(wave.size()));
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
        for (auto track : {loose.get(), packed.get()}) {
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

TEST_CASE_METHOD(AudioFixture, "Audio playback, spatialization and movie lifetime",
                 "[audio][mixer]")
{
    using wiz8::sound::Group;
    using wiz8::sound::PlayOptions;
    startOfflineAudio();
    unsigned callback_data = 0;
    const auto count_end = [&] {
        ++callbacks;
        ++callback_data;
    };
    const std::string_view path = "tone.wav";
    auto sound = wiz8::sound::play(path, {.volume = 127, .on_finished = count_end});
    REQUIRE((sound != nullptr && wiz8::sound::is_playing(sound)));
    REQUIRE(wiz8::sound::is_file_playing(path));
    auto position = wiz8::sound::playback_time(sound);
    REQUIRE(position.has_value());
    REQUIRE(position.value().total_ms == 100);
    auto samples = mix(2048);
    REQUIRE((energy(samples, 0) > 10 && energy(samples, 1) > 10));
    position = wiz8::sound::playback_time(sound);
    REQUIRE(position.has_value());
    REQUIRE((position.value().current_ms > 0 && position.value().current_ms <= 100));
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
    REQUIRE(wiz8::sound::is_file_playing(path));
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
    const std::string_view packed = "data\\packed.wav";
    sound = wiz8::sound::stream(packed);
    REQUIRE(sound != nullptr);
    position = wiz8::sound::playback_time(sound);
    REQUIRE((position.has_value() && position.value().total_ms == 100));
    samples = mix(8192);
    REQUIRE(energy(samples, 0) + energy(samples, 1) > 10);
    wiz8::sound::service();
    REQUIRE(!wiz8::sound::is_playing(sound));
    for (int i = 0; i < 64; ++i)
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
    for (auto x : {-3.0f, 0.0f, 3.0f}) {
        sound =
            wiz8::sound::play(path, {.volume = 127, .loops = 0, .position = glm::vec3(x, 0, 0)});
        REQUIRE(sound != nullptr);
        samples = mix(2048);
        const auto left = energy(samples, 0), right = energy(samples, 1);
        CAPTURE(x, left, right);
        REQUIRE(near(left, baseline * (x > 0 ? 0.04 : 1)));
        REQUIRE(near(right, baseline * (x < 0 ? 0.04 : 1)));
        REQUIRE(wiz8::sound::stop(sound));
    }
    // Callers apply distance falloff; the service only pans.
    for (auto distance : {1.0f, 100.0f}) {
        sound = wiz8::sound::play(
            path, {.volume = 127, .loops = 0, .position = glm::vec3(0, 0, -distance)});
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
    wiz8::sound::set_channel_volume(wiz8::sound::VolumeChannel::effects, 64);
    sound = wiz8::sound::play(path);
    REQUIRE(wiz8::sound::volume(sound) == 64u);
    REQUIRE(wiz8::sound::stop(sound));
    wiz8::sound::set_channel_volume(wiz8::sound::VolumeChannel::effects, 127);
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
    for (const std::string_view rejected : {"bad.wav", "data\\truncated.wav", ""}) {
        REQUIRE(wiz8::sound::play(rejected) == nullptr);
        REQUIRE(wiz8::sound::stream(rejected) == nullptr);
        REQUIRE(wiz8::sound::load(rejected) == nullptr);
    }
    REQUIRE(wiz8::sound::play(wiz8::sound::Clip{}) == nullptr);
    sound = wiz8::sound::stream(fallback, {.volume = 127});
    REQUIRE(wiz8::sound::is_file_playing(fallback));
    REQUIRE(sound != nullptr);
    samples = mix(8192);
    REQUIRE(energy(samples, 0) + energy(samples, 1) > 1);
    wiz8::sound::service();
    REQUIRE(!wiz8::sound::is_playing(sound));
    unsigned restarts = 0;
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
        for (size_t i = 0; i < pcm.size(); ++i)
            pcm[i] = float(std::sin(i * 2 * 3.14159265 * 440 / 44100) * 0.25);
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
        try {
            movie.append(pcm.data(), pcm.size() / 2);
        } catch (const std::exception&) {
            rejected = true;
        }
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
    const auto sound = wiz8::sound::play("tone.wav");
    REQUIRE(sound != nullptr);
    SDL_Delay(250);
    wiz8::sound::service();
    REQUIRE(!wiz8::sound::is_playing(sound));
    wiz8::sound::shutdown();
    puts("ok: native output device opened and completed playback");
}

TEST_CASE_METHOD(AudioFixture, "Audio clips outlive cache entries without slot reuse",
                 "[audio][ownership]")
{
    startOfflineAudio();
    std::vector<wiz8::sound::Clip> clips;
    for (unsigned i = 0; i < 140; ++i) {
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

TEST_CASE_METHOD(AudioFixture, "Owned clips retain the mixer across sound shutdown",
                 "[audio][ownership]")
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

TEST_CASE_METHOD(AudioFixture, "Playback references never alias replacement tracks",
                 "[audio][ownership]")
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

TEST_CASE_METHOD(AudioFixture, "Mixer service owns discarded playback until completion",
                 "[audio][ownership]")
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

TEST_CASE_METHOD(AudioFixture, "Paused playback survives completion polling", "[audio][ownership]")
{
    startOfflineAudio();
    unsigned calls = 0;
    auto sound = wiz8::sound::play("tone.wav", {.on_finished = [&] { ++calls; }});
    REQUIRE(sound);
    REQUIRE(MIX_PauseAllTracks(w8_native::audio_mixer()));
    mix(8192);
    wiz8::sound::service();
    REQUIRE(calls == 0);
    REQUIRE(wiz8::sound::volume(sound).has_value());
    REQUIRE(MIX_ResumeAllTracks(w8_native::audio_mixer()));
    REQUIRE(wiz8::sound::is_playing(sound));
    mix(8192);
    wiz8::sound::service();
    REQUIRE(calls == 1);
    REQUIRE_FALSE(wiz8::sound::volume(sound));
}

TEST_CASE_METHOD(AudioFixture, "Playback completion permits stop and shutdown reentry",
                 "[audio][ownership]")
{
    startOfflineAudio();
    wiz8::sound::Playback other, replacement;
    unsigned calls = 0;
    auto first = wiz8::sound::play("tone.wav", {.on_finished = [&] {
                                       ++calls;
                                       wiz8::sound::stop(other);
                                       replacement = wiz8::sound::play("tone.wav", {.loops = 0});
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
    REQUIRE(replacement);
    REQUIRE_FALSE(wiz8::sound::is_playing(replacement));
    wiz8::sound::service();
    REQUIRE(calls == 1);
}

TEST_CASE_METHOD(AudioFixture, "Stopping a group cancels its completion callbacks",
                 "[audio][ownership]")
{
    using wiz8::sound::Group;
    startOfflineAudio();
    unsigned calls = 0;
    auto first = wiz8::sound::play(
        "tone.wav", {.loops = 0, .group = Group::ambient, .on_finished = [&] { ++calls; }});
    REQUIRE(first);
    auto second = wiz8::sound::stream("tone.wav", {.loops = 0, .group = Group::ambient});
    REQUIRE(second);
    auto unrelated = wiz8::sound::play("tone.wav", {.loops = 0});
    REQUIRE(unrelated);
    auto music = wiz8::sound::play("tone.wav", {.loops = 0, .group = Group::music});
    REQUIRE(music);
    REQUIRE(wiz8::sound::stop_group(Group::ambient));
    REQUIRE_FALSE(wiz8::sound::is_playing(first));
    REQUIRE_FALSE(wiz8::sound::is_playing(second));
    REQUIRE(wiz8::sound::is_playing(unrelated));
    REQUIRE(wiz8::sound::is_playing(music));
    wiz8::sound::release_cache();
    REQUIRE_FALSE(wiz8::sound::is_playing(unrelated));
    REQUIRE(wiz8::sound::is_playing(music));
    REQUIRE(wiz8::sound::stop_group(Group::music));
    REQUIRE_FALSE(wiz8::sound::stop_group(Group::music));
    wiz8::sound::service();
    REQUIRE(calls == 0);
}

TEST_CASE_METHOD(AudioFixture, "Stopping completed playback cancels its deferred callback",
                 "[audio][ownership]")
{
    startOfflineAudio();
    unsigned calls = 0;
    auto sound = wiz8::sound::stream("tone.wav", {.on_finished = [&] { ++calls; }});
    REQUIRE(sound);
    mix(8192);
    REQUIRE_FALSE(wiz8::sound::is_playing(sound));
    REQUIRE_FALSE(wiz8::sound::stop(sound));
    wiz8::sound::service();
    REQUIRE(calls == 0);
}

TEST_CASE_METHOD(AudioFixture,
                 "Character event destruction cancels its own deferred voice callback",
                 "[audio][ownership]")
{
    startOfflineAudio();
    auto event = std::make_unique<W8CharacterEvent>(nullptr, 0, 0, 127);
    unsigned calls = 0;
    event->voice_playback = wiz8::sound::stream("tone.wav", {.on_finished = [&] { ++calls; }});
    REQUIRE(event->voice_playback);
    mix(8192);
    REQUIRE_FALSE(wiz8::sound::is_playing(event->voice_playback));
    REQUIRE(calls == 0);
    event.reset();
    REQUIRE(calls == 0);
    wiz8::sound::service();
    wiz8::sound::release_cache();
    REQUIRE(calls == 0);
}

TEST_CASE_METHOD(AudioFixture, "Only servicing audio dispatches deferred completion callbacks",
                 "[audio][ownership]")
{
    startOfflineAudio();
    unsigned calls = 0;
    auto first = wiz8::sound::stream("tone.wav", {.on_finished = [&] { ++calls; }});
    REQUIRE(first);
    mix(8192);
    REQUIRE_FALSE(wiz8::sound::is_playing(first));
    REQUIRE(calls == 0);
    auto next = wiz8::sound::play("tone.wav");
    REQUIRE(next);
    REQUIRE(calls == 0);
    wiz8::sound::service();
    REQUIRE(calls == 1);
}

TEST_CASE_METHOD(AudioFixture, "Button feedback preserves repeat and disabled silence",
                 "[audio][ui]")
{
    const auto directory = asset / "data" / "sound" / "misc";
    std::filesystem::create_directories(directory);
    for (const auto* name :
         {"interface click 01.wav", "interface click 02.wav", "interface whoosh 01 soft.wav"})
        write(directory / name, wave);
    startOfflineAudio();
    UiLayer layer;
    W8ButtonWidget button(layer);
    int activations = 0;
    REQUIRE(button.Configure(
        ButtonAppearance{}, {10, 10, 40, 20},
        {.on_click = [&](W8ButtonWidget*) { ++activations; },
         .policy = {.repeat = true, .input_rule = ButtonInputRule::PrimaryPressedState}}));
    const std::string_view release = "data\\sound\\misc\\interface click 01.wav";
    const std::string_view press = "data\\sound\\misc\\interface click 02.wav";
    auto dispatch = [&](InputEventType type, bool held) {
        InputEvent input{};
        input.type = type;
        input.position = {15, 15};
        input.buttons = held ? SDL_BUTTON_LMASK : 0;
        DispatchUiInput(&input);
    };
    dispatch(InputEventType::LeftButtonDown, true);
    REQUIRE(wiz8::sound::is_file_playing(press));
    for (int i = 0; i < 2; ++i) {
        wiz8::sound::stop_group(wiz8::sound::Group::effects);
        dispatch(InputEventType::LeftButtonRepeat, true);
        REQUIRE(wiz8::sound::is_file_playing(release));
    }
    REQUIRE(activations == 2);
    wiz8::sound::stop_group(wiz8::sound::Group::effects);
    dispatch(InputEventType::LeftButtonUp, false);
    REQUIRE_FALSE(wiz8::sound::is_file_playing(release));
    REQUIRE(activations == 2);
    button.SetEnabled(false);
    dispatch(InputEventType::LeftButtonDown, true);
    dispatch(InputEventType::LeftButtonRepeat, true);
    dispatch(InputEventType::LeftButtonUp, false);
    REQUIRE_FALSE(wiz8::sound::is_file_playing(press));
    REQUIRE_FALSE(wiz8::sound::is_file_playing(release));
    REQUIRE(activations == 2);
}

TEST_CASE_METHOD(AudioFixture,
                 "Unpublished worlds are silent and stop only their own ambient voices",
                 "[audio][world][ownership]")
{
    startOfflineAudio();
    std::ofstream(asset / "ambient.scf") << ".\nTone.wav 0 0 127 127 -1 -1 0\n";
    StandaloneWorld standalone_active;
    World& active = standalone_active.world;
    REQUIRE(ReadAmbientSoundListFile(active, "ambient.scf"));
    REQUIRE(active.audio.tracks.size() == 1);
    REQUIRE_FALSE(active.audio.tracks.front()->instance);
    StartAmbientSoundList(active);
    auto previous_voice = active.audio.tracks.front()->instance;
    REQUIRE(wiz8::sound::is_playing(previous_voice));
    wiz8::sound::Playback candidate_voice;
    {
        StandaloneWorld standalone_pending;
        World& pending = standalone_pending.world;
        REQUIRE(ReadAmbientSoundListFile(pending, "ambient.scf"));
        REQUIRE_FALSE(pending.audio.tracks.front()->instance);
        REQUIRE(wiz8::sound::is_playing(previous_voice));
        StartAmbientSoundList(pending);
        candidate_voice = pending.audio.tracks.front()->instance;
        REQUIRE(wiz8::sound::is_playing(candidate_voice));
    }
    REQUIRE_FALSE(wiz8::sound::is_playing(candidate_voice));
    REQUIRE(wiz8::sound::is_playing(previous_voice));
}

TEST_CASE_METHOD(AudioFixture, "Shared ambience retains its playback across region changes",
                 "[audio][ambient]")
{
    startOfflineAudio();
    SetSoundEffectsVolume(127);
    SharedAmbientGroup group;
    group.resource = "tone.wav";
    W8AmbientSound first, second, third;
    for (auto* emitter : {&first, &second, &third}) {
        emitter->radius = 100;
        emitter->volume_max = 80;
        emitter->bounded = 0;
        emitter->region_scale = glm::vec3(1);
        emitter->in_range = 1;
        group.emitters.push_back(emitter);
    }
    group.service(first);
    const auto playback = group.playback;
    REQUIRE(playback);
    group.current_volume = 37;
    first.in_range = 0;
    group.updatePosition(first, glm::vec3(0));
    REQUIRE(group.selected == &second);
    REQUIRE(group.playback == playback);
    REQUIRE(group.current_volume == 37);
    group.service(third);
    REQUIRE(group.selected == &second);
    REQUIRE(group.playback == playback);

    group.fade_timer.Pause();
    group.stop(second);
    group.service(third);
    REQUIRE(group.selected == &third);
    REQUIRE(group.playback);
    REQUIRE_FALSE(group.fade_timer.m_flags.paused);
    REQUIRE(group.target_volume == third.volume_max);
}

TEST_CASE_METHOD(AudioFixture, "Volume settings preserve source gain and ambient scheduling",
                 "[audio][volume]")
{
    startOfflineAudio();
    using wiz8::sound::VolumeChannel;
    game.game.level.replacePrimary(MakeWorld());
    auto emitter = std::make_unique<W8AmbientSound>();
    emitter->in_range = 1;
    emitter->current_volume = 73;
    emitter->random = std::make_unique<W8AmbientTrack>();
    emitter->random->next_at = 12345;
    auto* observed = emitter.get();
    GetWorld()->audio.emitters.push_back(std::move(emitter));
    auto effect = wiz8::sound::play("tone.wav", {.volume = 100, .loops = 0});
    auto voice =
        wiz8::sound::play("tone.wav", {.volume = 90, .channel = VolumeChannel::voice, .loops = 0});
    auto positional = wiz8::sound::play("tone.wav", {.volume = 3, .loops = 0});
    wiz8::sound::set_volume(positional, 3, 0.9f, wiz8::sound::VolumeRounding::after_attenuation);
    auto expected_rng = PresentationRng();
    SetSoundEffectsVolume(0);
    REQUIRE(wiz8::sound::volume(effect) == 0);
    REQUIRE(wiz8::sound::volume(voice) == 90);
    SetSoundEffectsVolume(64);
    REQUIRE(wiz8::sound::volume(effect) == 50);
    REQUIRE(wiz8::sound::volume(positional) == 1);
    REQUIRE(observed->in_range == 1);
    REQUIRE(observed->current_volume == 73);
    REQUIRE(observed->random->next_at == 12345);
    REQUIRE(PresentationRng().next() == expected_rng.next());
    SetSoundEffectsVolume(127);
    REQUIRE(wiz8::sound::volume(effect) == 100);
    REQUIRE(wiz8::sound::volume(positional) == 2);
}
