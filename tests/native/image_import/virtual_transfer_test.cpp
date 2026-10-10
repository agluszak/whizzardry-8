#include "surrender/srImageIO.h"
#include "image_stream.h"
#include "wiz8/engine_code/stTextureFile.h"
#include "wiz8/sr_api.h"
#include "wiz8/slf.h"
#include "../temporary_directory.h"
#include <wiz8/filesystem.h>
#include <wiz8/asset_paths.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "line %d: %s\n", __LINE__, #e); \
    throw std::runtime_error(#e); } } while (0)
using Bytes = std::vector<unsigned char>;
namespace fs = std::filesystem;
using Surface = std::unique_ptr<srColorSurfaceIFace, void(*)(srColorSurfaceIFace*)>;
static Surface own(srColorSurfaceIFace* p)
{
    return {p, [](srColorSurfaceIFace* s) { if (s) s->release(); }};
}
static void write(const fs::path& path, const Bytes& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    CHECK(output.good());
}
static Bytes grayFixture()
{
    std::ifstream input(fs::path(WIZ8_IMAGE_IMPORT_FIXTURE_DIR) / "gray.jpg", std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>(input), {}};
}
static Bytes tgaFixture()
{
    Bytes bytes(18);
    bytes[2] = 2; bytes[12] = 3; bytes[14] = 2; bytes[16] = 16; bytes[17] = 0x30;
    bytes.insert(bytes.end(), {0xe0, 0x03, 0x03, 0xfc, 0, 0, 0xff, 0xff, 0, 0x7c, 0x1f, 0});
    return bytes;
}
static Bytes archive(const Bytes& jpeg, const Bytes& tga)
{
    wiz8::SlfHeader header{};
    snprintf(header.sLibName, sizeof(header.sLibName), "%s", "Data.slf");
    snprintf(header.sPathToLibrary, sizeof(header.sPathToLibrary), "%s", "Data\\");
    header.iEntries = header.iUsed = 4; header.iVersion = 0x200;
    wiz8::SlfEntry entries[4]{};
    auto broken_jpeg = jpeg; broken_jpeg.resize(jpeg.size() - 2);
    auto broken_tga = tga; broken_tga.pop_back();
    const Bytes* payloads[] = {&broken_jpeg, &jpeg, &broken_tga, &tga};
    const char* names[] = {"Broken.jpg", "Archive.jpg", "Broken.tga", "Archive.tga"};
    Bytes bytes(reinterpret_cast<const unsigned char*>(&header),
                reinterpret_cast<const unsigned char*>(&header) + sizeof(header));
    for (unsigned i = 0; i < 4; ++i) {
        snprintf(entries[i].sFileName, sizeof(entries[i].sFileName), "%s", names[i]);
        entries[i].uiOffset = bytes.size(); entries[i].uiLength = payloads[i]->size();
        bytes.insert(bytes.end(), payloads[i]->begin(), payloads[i]->end());
    }
    const auto* directory = reinterpret_cast<const unsigned char*>(entries);
    bytes.insert(bytes.end(), directory, directory + sizeof(entries));
    return bytes;
}
static void checkGray(srColorSurfaceIFace* surface)
{
    CHECK(surface && surface->getWidth() == 3 && surface->getHeight() == 2);
    for (unsigned y = 0; y < 2; ++y) {
        const auto* row = static_cast<const unsigned char*>(surface->getDataPtr()) + y * surface->getPitch();
        for (unsigned x = 0; x < 3; ++x) CHECK(abs(int(row[x]) - int(20 + (y * 3 + x) * 9)) <= 1);
    }
}
static void checkTga(const char* name, bool succeeds)
{
    std::string path(name);
    auto handle = [&]() { try { return wiz8::open_file(path.data(), wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    CHECK(handle);
    auto surface = own(LoadSurface(handle.get(), nullptr));
    CHECK(bool(surface) == succeeds);
    if (surface) {
        const unsigned char expected[] = {0, 0, 0x03, 0xfc, 0xe0, 0x03, 0x1f, 0, 0, 0x7c, 0xff, 0xff};
        CHECK(surface->getWidth() == 3 && surface->getHeight() == 2);
        for (unsigned y = 0; y < 2; ++y)
            CHECK(memcmp(static_cast<const unsigned char*>(surface->getDataPtr()) + y * surface->getPitch(),
                         expected + y * 6, 6) == 0);
    }
    CHECK(handle->size() > 0 && (handle->seek(0, wiz8::SeekOrigin::begin), true));
    unsigned char signature = 255; unsigned int count = 0;
    CHECK(((count = handle->read(&signature, 1).bytes) == static_cast<std::size_t>(1)) && count == 1 && signature == 0);
    handle.reset();
}
static void runtimeImages()
{
    for (const char* name : {"C:/DATA/loose.jpeg", "Data/archive.jpg", "Data/zażółć-雪.jpeg",
                             "D:/Data/disc.jpg", "E:/data/DISC.JPG", "F:/data/disc.jpg"}) {
        auto surface = own(srImage::load(name));
        checkGray(surface.get());
        srColorSurfaceIFace::SurfaceDesc desc;
        srImage::describe(desc, name);
        CHECK(desc.width == 3 && desc.height == 2);
    }
    for (const char* name : {"C:/data/LOOSE.tga", "Data/ARCHIVE.TGA"}) {
        auto surface = own(srImage::load(name));
        CHECK(surface && surface->getWidth() == 3 && surface->getHeight() == 2);
        srColorSurfaceIFace::SurfaceDesc desc;
        srImage::describe(desc, name);
        CHECK(desc.width == 3 && desc.height == 2);
        const unsigned char expected[] = {0, 0, 0x03, 0xfc, 0xe0, 0x03, 0x1f, 0, 0, 0x7c, 0xff, 0xff};
        for (unsigned y = 0; y < 2; ++y)
            CHECK(memcmp(static_cast<const unsigned char*>(surface->getDataPtr()) + y * surface->getPitch(),
                         expected + y * 6, 6) == 0);
    }
    for (const char* name : {"Data/Broken.jpg", "Data/Broken.tga", "Data/missing.jpeg",
                             "jpg://Data/archive.jpg", "tga://Data/archive.tga", "file://Data/archive.jpg"}) {
        bool rejected = false;
        try { auto surface = own(srImage::load(name)); }
        catch (const std::exception&) { rejected = true; }
        CHECK(rejected);
        rejected = false;
        try { srColorSurfaceIFace::SurfaceDesc desc; srImage::describe(desc, name); }
        catch (const std::exception&) { rejected = true; }
        CHECK(rejected);
    }
    auto surface = own(srImage::load("Data/archive.jpg"));
    srImage::save("C:/capture.jpeg", *surface, 35);
    auto capture = own(srImage::load("C:/capture.jpeg"));
    CHECK(capture && capture->getWidth() == 3 && capture->getHeight() == 2);
    for (const char* name : {"capture.png", "capture.bmp"}) {
        auto output = wiz8::open_file(name, wiz8::OpenMode::replace);
        srImage::save(name, *output, *surface);
        CHECK(output->is_open() && output->size() > 0);
        output->close();
        auto decoded = own(srImage::load(name));
        CHECK(decoded && decoded->getWidth() == 3 && decoded->getHeight() == 2);
        for (unsigned y = 0; y < 2; ++y)
            for (unsigned x = 0; x < 3; ++x)
                CHECK(decoded->getPixel(x, y) == surface->getPixel(x, y));
    }
}
static void checkUninitialized()
{
    auto file = wiz8::open_file("Data/archive.jpg");
    file->seek(9, wiz8::SeekOrigin::begin);
    CHECK(!srImage::load("archive.jpg", *file) && file->tell() == 9);
    CHECK(!srImage::loadTga(*file) && file->tell() == 9);
    bool rejected = false;
    try { auto surface = own(srImage::load("Data/archive.jpg")); }
    catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);
}
int main() try
{
    const auto root = fs::path(make_temporary_directory("sr-image-virtual"));
    const auto assets = root / "assets", user = root / "user", disc = root / "disc";
    const auto jpeg = grayFixture(), tga = tgaFixture();
    write(assets / "data" / "DATA.SLF", archive(jpeg, tga));
    write(assets / "data" / "Loose.JpEg", jpeg);
    write(assets / "data" / "zażółć-雪.JpEg", jpeg);
    write(assets / "data" / "Loose.TGA", tga);
    write(disc / "data" / "Disc.JPG", jpeg);
    fs::create_directories(user);
    w8_native::configure_paths({assets.string(), user.string(), {disc.string(), disc.string(), disc.string()}});
    wiz8::mount_slf("Data\\Data.slf");
    checkUninitialized();
    CHECK(srInit());
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        runtimeImages();
        CHECK(fs::exists(user / "capture.jpeg") && !fs::exists(assets / "capture.jpeg"));
        srExit();
        CHECK(srInit());
    }
    {
        for (const char* name : {"C:/DATA/loose.jpeg", "D:/Data/disc.jpg", "E:/data/DISC.JPG", "F:/data/disc.jpg"}) {
            auto stream = wiz8::open_file(name);
            auto surface = own(srImage::load(name, *stream));
            checkGray(surface.get());
            stream->seek(0, wiz8::SeekOrigin::begin);
            unsigned char byte = 0; stream->read_exact(&byte, 1);
            CHECK(stream->is_open() && byte == 0xff);
        }
        auto first = wiz8::open_file("C:/DATA/archive.jpg"), second = wiz8::open_file("Data\\ARCHIVE.JPG");
        first->seek(9, wiz8::SeekOrigin::begin);
        srColorSurfaceIFace::SurfaceDesc desc;
        CHECK(srImage::describe(desc, "archive.jpg", *first) && first->tell() == 9);
        auto surface = own(srImage::load("archive.jpg", *second));
        checkGray(surface.get());
        CHECK(first->tell() == 9 && first->is_open());
        auto broken = wiz8::open_file("Data/Broken.jpg");
        broken->seek(7, wiz8::SeekOrigin::begin);
        CHECK(!srImage::describe(desc, "broken.jpg", *broken) && broken->tell() == 7);
        CHECK(!srImage::load("broken.jpg", *broken) && broken->is_open());
        {
            srImage::Stream bridge{*first};
            auto io = bridge.open();
            CHECK(io && SDL_GetIOSize(io.get()) == Sint64(jpeg.size()));
            CHECK(SDL_SeekIO(io.get(), -1, SDL_IO_SEEK_END) == Sint64(jpeg.size() - 1));
            unsigned char bytes[8];
            CHECK(SDL_ReadIO(io.get(), bytes, sizeof(bytes)) == 1 && bytes[0] == 0xd9);
            CHECK(SDL_ReadIO(io.get(), bytes, sizeof(bytes)) == 0 && bridge.eof && !bridge.failed);
        }
        for (const auto offset : {std::numeric_limits<Sint64>::min(), std::numeric_limits<Sint64>::max()}) {
            first->seek(9, wiz8::SeekOrigin::begin);
            srImage::Stream bridge{*first};
            auto io = bridge.open();
            CHECK(SDL_SeekIO(io.get(), offset, SDL_IO_SEEK_CUR) == -1 && bridge.failed);
            CHECK(first->tell() == 9);
        }
        first->close();
        CHECK(!srImage::load("archive.jpg", *first));
        CHECK(!srImage::describe(desc, "archive.jpg", *first));
        for (auto* file : {first.get(), second.get()}) {
            bool rejected = false;
            try { srImage::save("capture.jpg", *file, *surface); }
            catch (const std::exception&) { rejected = true; }
            CHECK(rejected);
        }
        CHECK(LoadSurface(nullptr, nullptr) == nullptr);
        checkTga("C:/data/LOOSE.tga", true);
        checkTga("Data/ARCHIVE.TGA", true);
        checkTga("Data/Broken.tga", false);
        write(user / "data" / "Loose.jpeg", {0, 1, 2});
        auto overlay = wiz8::open_file("Data/loose.jpeg");
        CHECK(!srImage::load("loose.jpeg", *overlay) && overlay->is_open());
        checkTga("Data/Broken.tga", false);
    }
    srExit();
    checkUninitialized();
    wiz8::clear_asset_archives(); (void)0;
    fs::remove_all(root);
    puts("Virtual image streams: direct load/save, reinit, C-F, retail case folding, overlays, SLF bounds, independent cursors and borrowed handles passed");
    return 0;
}
catch (...) { srExit(); wiz8::clear_asset_archives(); (void)0; return 1; }
