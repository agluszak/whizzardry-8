#include "../../src/native/movie.h"
#include "FileMan.h"
#include "LibraryDataBase.h"
#include "compat/surfaces.h"
#include "native/audio_test.h"
#include <wiz8/asset_paths.h>
#include "soundman.h"
#include "wiz8/bink_video.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
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
std::vector<unsigned char> read(const char* path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Missing test fixture");
    std::vector<unsigned char> bytes(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
        throw std::runtime_error("Short fixture");
    return bytes;
}
void write(const std::filesystem::path& path, const std::vector<unsigned char>& bytes)
{
    std::ofstream file(path, std::ios::binary);
    if (!file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()))
        throw std::runtime_error("Cannot create fixture");
}
struct Temporary
{
    std::filesystem::path path;
    ~Temporary()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
bool matchesFrame(const std::vector<uint16_t>& pixels, const unsigned char* golden)
{
    unsigned largest_difference = 0;
    for (size_t i = 0; i < pixels.size(); ++i)
    {
        const unsigned expected = golden[i * 2] | (unsigned(golden[i * 2 + 1]) << 8);
        if ((pixels[i] & 0x8000) != (expected & 0x8000))
            return false;
        for (unsigned shift : {0u, 5u, 10u})
        {
            const int difference = int((pixels[i] >> shift) & 31) - int((expected >> shift) & 31);
            largest_difference = std::max(largest_difference, unsigned(std::abs(difference)));
        }
    }
    if (largest_difference > 1)
        fprintf(stderr, "movie RGB555 maximum channel difference: %u\n", largest_difference);
    // libswscale versions/CPU paths can round by one RGB555 quantization step.
    return largest_difference <= 1;
}
int main(int argc, char** argv)
{
    try
    {
        const auto temporary = make_temporary_directory("wiz8-movie");
        Temporary fixture{temporary};
        auto assets = fixture.path / "assets", user = fixture.path / "user";
        std::filesystem::create_directories(assets / "Data");
        std::filesystem::create_directories(user);
        w8_native::configure_paths({assets.string(), user.string(), {}});
        auto encoded = read(WIZ8_MOVIE_FIXTURE), golden = read(WIZ8_MOVIE_GOLDEN);
        CHECK(golden.size() == 5 * 1536);
        write(assets / "Movie.mkv", encoded);
        LIBHEADER header{};
        strcpy(header.sLibName, "DATA.SLF");
        strcpy(header.sPathToLibrary, "Data\\");
        header.iEntries = header.iUsed = 2;
        header.iVersion = 0x200;
        DIRENTRY entries[2]{};
        strcpy(entries[0].sFileName, "Packed.mkv");
        entries[0].uiOffset = sizeof(header);
        entries[0].uiLength = encoded.size();
        strcpy(entries[1].sFileName, "Truncated.mkv");
        entries[1].uiOffset = sizeof(header);
        entries[1].uiLength = 16; // The rest of the valid movie must be inaccessible.
        std::vector<unsigned char> archive(reinterpret_cast<unsigned char*>(&header),
                                           reinterpret_cast<unsigned char*>(&header) +
                                               sizeof(header));
        archive.insert(archive.end(), encoded.begin(), encoded.end());
        archive.insert(archive.end(), reinterpret_cast<unsigned char*>(entries),
                       reinterpret_cast<unsigned char*>(entries) + sizeof(entries));
        write(assets / "Data" / "DATA.SLF", archive);
        CHECK(InitializeFileManager(nullptr));
        CHECK(InitializeFileDatabase());
        w8_native::audio_offline_for_test(true);
        CHECK(InitializeSoundManager());
        double energy = 0;
        std::vector<uint16_t> first_frame;
        {
            W8NativeVideo movies[3];
            movies[0].open("C:\\MOVIE.MKV");
            movies[1].open("data\\PACKED.mkv");
            movies[2].open("Data\\Packed.mkv");
            unsigned shown[3]{};
            bool done[3]{};
            for (int tick = 0; tick < 700; ++tick)
            {
                for (unsigned i = 0; i < 3; ++i)
                {
                    if (done[i])
                        continue;
                    auto result = movies[i].update(tick / 1000.0);
                    if (result == W8NativeVideo::FrameReady)
                    {
                        const auto& frame = movies[i].frame();
                        CHECK(shown[i] < 5 && frame.width == 32 && frame.height == 24);
                        CHECK(std::abs(frame.time - shown[i] * 0.1) < 0.000001);
                        CHECK(matchesFrame(frame.pixels, golden.data() + shown[i] * 1536));
                        if (i == 0 && shown[i] == 0)
                            first_frame = frame.pixels;
                        ++shown[i];
                    }
                    if (result == W8NativeVideo::Done)
                    {
                        CHECK(tick >= 500);
                        done[i] = true;
                    }
                }
                if (done[0] && done[1] && done[2])
                    break;
                std::vector<float> samples((tick % 10 == 9 ? 45 : 44) * 2);
                CHECK(w8_native::audio_render_for_test(samples.data(), samples.size() / 2));
                for (auto value : samples)
                    energy += value * value;
            }
            for (unsigned i = 0; i < 3; ++i)
            {
                CHECK(done[i] && shown[i] == 5 && movies[i].decoded_frames() == 5);
                CHECK(movies[i].decoded_audio_frames() == 22050);
            }
        }
        CHECK(energy > 10);
        {
            W8NativeVideo reusable;
            for (const char* name : {"Data\\Truncated.mkv", "missing.mkv"})
            {
                reusable.open("Movie.mkv");
                bool failed = false;
                try
                {
                    reusable.open(name);
                }
                catch (const std::exception&)
                {
                    failed = true;
                }
                CHECK(failed && reusable.decoded_frames() == 0 &&
                      reusable.decoded_audio_frames() == 0);
            }
            reusable.open("Data\\Packed.mkv");
            CHECK(reusable.update(0) == W8NativeVideo::FrameReady);
            CHECK(matchesFrame(reusable.frame().pixels, golden.data()));
        }
        auto target = CreateCpuSurface(640, 480, 16, 0x7c00, 0x3e0, 0x1f);
        SurfaceLock description{};
        {
            W8BinkVideo movie;
            movie.SetTarget(target.get());
            CHECK(movie.Open("Movie.mkv", 0));
            CHECK(!movie.UpdateFrame());
            description = LockCpuSurface(*target);
            for (int y = 0; y < 24; ++y)
                CHECK(std::memcmp(static_cast<unsigned char*>(description.pixels) +
                                      y * description.pitch,
                                  first_frame.data() + y * 32, 64) == 0);
            UnlockCpuSurface(*target);
            CHECK(movie.Open("Data\\Packed.mkv",
                             0)); // Reopen stops the preceding PCM voice.
            CHECK(!movie.UpdateFrame());
            CHECK(!movie.Open("Movie.mkv", 1));
            CHECK(!movie.Open("missing.bik", 0));
        }
        target.reset();
        std::vector<float> silence(8192 * 2);
        CHECK(w8_native::audio_render_for_test(silence.data(), 8192));
        double residual = 0;
        for (size_t i = 4096; i < silence.size(); ++i)
            residual += silence[i] * silence[i];
        CHECK(residual == 0);
        if (argc > 1)
        {
            W8NativeVideo retail;
            retail.open(argv[1]);
            uint64_t hash = 14695981039346656037ull;
            unsigned shown = 0;
            bool done = false;
            for (int tick = 0; tick < 600000; ++tick)
            {
                auto result = retail.update(tick / 1000.0);
                if (result == W8NativeVideo::FrameReady)
                {
                    ++shown;
                    const auto& frame = retail.frame();
                    for (auto word : frame.pixels)
                    {
                        hash = (hash ^ (word & 255)) * 1099511628211ull;
                        hash = (hash ^ (word >> 8)) * 1099511628211ull;
                    }
                }
                if (result == W8NativeVideo::Done)
                {
                    done = true;
                    break;
                }
                CHECK(w8_native::audio_render_for_test(silence.data(), tick % 10 == 9 ? 45 : 44));
            }
            CHECK(done && shown == retail.decoded_frames());
            printf("retail movie: %u frames, %llu PCM frames, RGB555 FNV64 %016llx\n", shown,
                   (unsigned long long)retail.decoded_audio_frames(), (unsigned long long)hash);
        }
        ShutdownSoundManager();
        ShutDownFileDatabase();
        ShutdownFileManager();
        printf("movie: loose/SLF RGB555 golden frames, timed EOF, PCM audio, "
               "reopen and bounded failure passed\n");
        return 0;
    }
    catch (const std::exception& failure)
    {
        fprintf(stderr, "movie test: %s\n", failure.what());
        return 1;
    }
}
