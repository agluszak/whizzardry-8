/* Recovered soundman -> native miniaudio, with deterministic offline mixing. */
#include "native/audio_test.h"
#include "FileMan.h"
#include "LibraryDataBase.h"
#include "MemMan.h"
#include "compat/audio.h"
#include "compat/platform.h"
#include "platform_paths.h"
#include "soundman.h"
#include <wiz8/filesystem.h>
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
static void word(std::vector<unsigned char>& data, unsigned value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        data.push_back(value >> (i * 8));
}
static void tag(std::vector<unsigned char>& data, const char* value)
{
    data.insert(data.end(), value, value + 4);
}
static std::vector<unsigned char> wav()
{
    std::vector<unsigned char> bytes;
    tag(bytes, "RIFF");
    word(bytes, 36 + 4410 * 2, 4);
    tag(bytes, "WAVE");
    tag(bytes, "fmt ");
    word(bytes, 16, 4);
    word(bytes, 1, 2);
    word(bytes, 1, 2);
    word(bytes, 44100, 4);
    word(bytes, 88200, 4);
    word(bytes, 2, 2);
    word(bytes, 16, 2);
    tag(bytes, "data");
    word(bytes, 4410 * 2, 4);
    for (int i = 0; i < 4410; ++i)
        word(bytes, unsigned(short(std::sin(i * 2 * 3.14159265 * 440 / 44100) * 12000)), 2);
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
    std::filesystem::create_directories(asset / "Data");
    LIBHEADER header{};
    strcpy(header.sLibName, "Data.slf");
    strcpy(header.sPathToLibrary, "Data\\");
    header.iEntries = header.iUsed = 2;
    header.iVersion = 0x200;
    DIRENTRY entries[2]{};
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
    CHECK(InitializeMemoryManager());
    CHECK(InitializeFileManager(nullptr));
    CHECK(InitializeFileDatabase());
    {
        char packed[] = "data\\PACKED.WAV";
        const HWFILE entry = FileOpen(packed, FILE_ACCESS_READ | FILE_OPEN_EXISTING, FALSE);
        CHECK(entry);
        std::unique_ptr<wiz8::File> first(OpenLibraryStream(entry)), second(OpenLibraryStream(entry));
        FileClose(entry);
        CHECK(first && second && first->tell() == sizeof(header) &&
              second->tell() == sizeof(header));
        auto loose = wiz8::open_file("C:\\TONE.wav");
        unsigned char bytes[64]{};
        CHECK(first->read(bytes, sizeof(bytes)).bytes == sizeof(bytes));
        CHECK(std::memcmp(bytes, wave.data(), sizeof(bytes)) == 0);
        CHECK(second->tell() == sizeof(header) && loose->tell() == 0);
        CHECK(second->read(bytes, 32).bytes == 32);
        CHECK(std::memcmp(bytes, wave.data(), 32) == 0);
        first.reset();
        CHECK(second->seek(sizeof(header) + wave.size() - 32, wiz8::SeekOrigin::begin) ==
              int64_t(sizeof(header) + wave.size() - 32));
        CHECK(second->read(bytes, 32).bytes == 32);
        CHECK(std::memcmp(bytes, wave.data() + wave.size() - 32, 32) == 0);
        CHECK(loose->read(bytes, sizeof(bytes)).bytes == sizeof(bytes));
        CHECK(std::memcmp(bytes, wave.data(), sizeof(bytes)) == 0);
    }
    w8_native::audio_offline_for_test(true);
    Sound3DSetProvider(const_cast<char*>("legacy provider name"));
    CHECK(InitializeSoundManager());
    {
        using Stream = std::unique_ptr<_STREAM, decltype(&AIL_close_stream)>;
        const auto driver = SoundGetDriverHandle();
        Stream loose(AIL_open_stream(driver, "C:\\tone.wav", 0), AIL_close_stream);
        Stream packed(AIL_open_stream(driver, "Data\\Packed.wav", 0), AIL_close_stream);
        Stream looping(AIL_open_stream(driver, "data\\PACKED.WAV", 0), AIL_close_stream);
        CHECK(loose && packed && looping);
        CHECK(!AIL_open_stream(driver, "Data\\Truncated.wav", 0));
        CHECK(!AIL_open_stream(driver, "missing.wav", 0));
        AIL_set_stream_loop_count(looping.get(), 0);
        for (auto stream : {loose.get(), packed.get(), looping.get()})
        {
            S32 total = 0, current = 0;
            AIL_stream_ms_position(stream, &total, &current);
            CHECK(total == 100 && current == 0);
            AIL_start_stream(stream);
        }
        auto samples = mix(8192);
        CHECK(energy(samples, 0) + energy(samples, 1) > 10);
        CHECK(AIL_stream_status(loose.get()) == SMP_DONE);
        CHECK(AIL_stream_status(packed.get()) == SMP_DONE);
        CHECK(AIL_stream_status(looping.get()) == SMP_PLAYING);
        loose.reset();
        packed.reset();
        samples = mix(8192);
        CHECK(AIL_stream_status(looping.get()) == SMP_PLAYING);
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
        CHECK(SoundGetDriverHandle());
        memset(&options, 0xff, sizeof(options));
        options.uiLoop = 1;
        sound = SoundPlay(path, &options);
        CHECK(sound != SOUND_ERROR);
        Sleep(250);
        SoundServiceStreams();
        CHECK(!SoundIsPlaying(sound));
        ShutdownSoundManager();
        puts("ok: native output device opened and completed playback");
    }
    ShutDownFileDatabase();
    ShutdownFileManager();
    ShutdownMemoryManager();
    std::filesystem::remove_all(temporary);
    puts("ok: native audio samples, streams, loops, pans, fades, callbacks, spatialization and "
         "lifetime");
}
