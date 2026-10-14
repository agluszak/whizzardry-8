#include <catch2/catch_test_macros.hpp>
#include <ios>
#include <wiz8/filesystem.h>
#include <wiz8/asset_paths.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <vector>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

#include "surrender/srImageIO.h"
#include "wiz8/engine_code/stTextureFile.h"
#include "wiz8/sr_api.h"
#include "wiz8/slf.h"
#include "../temporary_directory.h"
#include "surrender/srColorSurface.h"
#include "surrender/srColorSurfaceIFace.h"

using Bytes = std::vector<unsigned char>;
namespace fs = std::filesystem;
using Surface = std::unique_ptr<srColorSurfaceIFace>;
static void write(const fs::path& path, const Bytes& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    REQUIRE(output.good());
}
static Bytes grayFixture()
{
    std::ifstream input(fs::path(WIZ8_IMAGE_IMPORT_FIXTURE_DIR) / "gray.jpg", std::ios::binary);
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>(input), {}};
}
static Bytes tgaFixture()
{
    Bytes bytes(18);
    bytes[2] = 2;
    bytes[12] = 3;
    bytes[14] = 2;
    bytes[16] = 16;
    bytes[17] = 0x30;
    bytes.insert(bytes.end(), {0xe0, 0x03, 0x03, 0xfc, 0, 0, 0xff, 0xff, 0, 0x7c, 0x1f, 0});
    return bytes;
}
static Bytes archive(const Bytes& jpeg, const Bytes& tga)
{
    wiz8::SlfHeader header{};
    snprintf(header.sLibName, sizeof(header.sLibName), "%s", "Data.slf");
    snprintf(header.sPathToLibrary, sizeof(header.sPathToLibrary), "%s", "Data\\");
    header.iEntries = header.iUsed = 4;
    header.iVersion = 0x200;
    wiz8::SlfEntry entries[4]{};
    auto broken_jpeg = jpeg;
    broken_jpeg.resize(jpeg.size() - 2);
    auto broken_tga = tga;
    broken_tga.pop_back();
    const Bytes* payloads[] = {&broken_jpeg, &jpeg, &broken_tga, &tga};
    const char* names[] = {"Broken.jpg", "Archive.jpg", "Broken.tga", "Archive.tga"};
    Bytes bytes(reinterpret_cast<const unsigned char*>(&header),
                reinterpret_cast<const unsigned char*>(&header) + sizeof(header));
    for (unsigned i = 0; i < 4; ++i) {
        snprintf(entries[i].sFileName, sizeof(entries[i].sFileName), "%s", names[i]);
        entries[i].uiOffset = bytes.size();
        entries[i].uiLength = payloads[i]->size();
        bytes.insert(bytes.end(), payloads[i]->begin(), payloads[i]->end());
    }
    const auto* directory = reinterpret_cast<const unsigned char*>(entries);
    bytes.insert(bytes.end(), directory, directory + sizeof(entries));
    return bytes;
}
static void checkGray(srColorSurfaceIFace* surface)
{
    REQUIRE((surface && surface->getWidth() == 3 && surface->getHeight() == 2));
    for (unsigned y = 0; y < 2; ++y) {
        const auto* row =
            static_cast<const unsigned char*>(surface->getDataPtr()) + y * surface->getPitch();
        for (unsigned x = 0; x < 3; ++x) {
            REQUIRE(abs(int(row[x]) - int(20 + (y * 3 + x) * 9)) <= 1);
        }
    }
}
static void checkTga(const char* name, bool succeeds)
{
    std::string path(name);
    auto handle = [&]() {
        try {
            return wiz8::open_file(path.data(), wiz8::OpenMode::read);
        } catch (const std::exception&) {
            return std::unique_ptr<wiz8::File>{};
        }
    }();
    REQUIRE(handle);
    auto surface = LoadSurface(handle.get(), nullptr);
    REQUIRE(bool(surface) == succeeds);
    if (surface) {
        const unsigned char expected[] = {0,    0, 0x03, 0xfc, 0xe0, 0x03,
                                          0x1f, 0, 0,    0x7c, 0xff, 0xff};
        REQUIRE((surface->getWidth() == 3 && surface->getHeight() == 2));
        for (unsigned y = 0; y < 2; ++y) {
            REQUIRE(memcmp(static_cast<const unsigned char*>(surface->getDataPtr()) +
                               y * surface->getPitch(),
                           expected + y * 6, 6) == 0);
        }
    }
    REQUIRE((handle->size() > 0 && (handle->seek(0, wiz8::SeekOrigin::begin), true)));
    unsigned char signature = 255;
    unsigned int count = 0;
    REQUIRE((((count = handle->read(&signature, 1).bytes) == static_cast<std::size_t>(1)) &&
             count == 1 && signature == 0));
    handle.reset();
}
static void runtimeImages()
{
    for (const char* name : {"C:/DATA/loose.jpeg", "Data/archive.jpg", "Data/zażółć-雪.jpeg",
                             "D:/Data/disc.jpg", "E:/data/DISC.JPG", "F:/data/disc.jpg"}) {
        auto surface = srImage::load(name);
        checkGray(surface.get());
        srColorSurfaceIFace::SurfaceDesc desc;
        srImage::describe(desc, name);
        REQUIRE((desc.width == 3 && desc.height == 2));
    }
    for (const char* name : {"C:/data/LOOSE.tga", "Data/ARCHIVE.TGA"}) {
        auto surface = srImage::load(name);
        REQUIRE((surface && surface->getWidth() == 3 && surface->getHeight() == 2));
        srColorSurfaceIFace::SurfaceDesc desc;
        srImage::describe(desc, name);
        REQUIRE((desc.width == 3 && desc.height == 2));
        const unsigned char expected[] = {0,    0, 0x03, 0xfc, 0xe0, 0x03,
                                          0x1f, 0, 0,    0x7c, 0xff, 0xff};
        for (unsigned y = 0; y < 2; ++y) {
            REQUIRE(memcmp(static_cast<const unsigned char*>(surface->getDataPtr()) +
                               y * surface->getPitch(),
                           expected + y * 6, 6) == 0);
        }
    }
    for (const char* name :
         {"Data/Broken.jpg", "Data/Broken.tga", "Data/missing.jpeg", "jpg://Data/archive.jpg",
          "tga://Data/archive.tga", "file://Data/archive.jpg"}) {
        bool rejected = false;
        try {
            auto surface = srImage::load(name);
        } catch (const std::exception&) {
            rejected = true;
        }
        REQUIRE(rejected);
        rejected = false;
        try {
            srColorSurfaceIFace::SurfaceDesc desc;
            srImage::describe(desc, name);
        } catch (const std::exception&) {
            rejected = true;
        }
        REQUIRE(rejected);
    }
    auto surface = srImage::load("Data/archive.jpg");
    srImage::save("C:/capture.jpeg", *surface, 35);
    auto capture = srImage::load("C:/capture.jpeg");
    REQUIRE((capture && capture->getWidth() == 3 && capture->getHeight() == 2));
    for (const char* name : {"capture.png", "capture.bmp"}) {
        auto output = wiz8::open_file(name, wiz8::OpenMode::replace);
        srImage::save(name, *output, *surface);
        REQUIRE((output->is_open() && output->size() > 0));
        output->close();
        auto decoded = srImage::load(name);
        REQUIRE((decoded && decoded->getWidth() == 3 && decoded->getHeight() == 2));
        for (unsigned y = 0; y < 2; ++y) {
            for (unsigned x = 0; x < 3; ++x) {
                REQUIRE(decoded->getPixel(x, y) == surface->getPixel(x, y));
            }
        }
    }
}
static void checkUninitialized()
{
    auto file = wiz8::open_file("Data/archive.jpg");
    file->seek(9, wiz8::SeekOrigin::begin);
    REQUIRE((!srImage::load("archive.jpg", *file) && file->tell() == 9));
    REQUIRE((!srImage::loadTga(*file) && file->tell() == 9));
    bool rejected = false;
    try {
        auto surface = srImage::load("Data/archive.jpg");
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    REQUIRE(rejected);
}
TEST_CASE("virtual_transfer_test")
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
    w8_native::configure_paths(
        {assets.string(), user.string(), {disc.string(), disc.string(), disc.string()}});
    wiz8::mount_slf("Data\\Data.slf");
    checkUninitialized();
    REQUIRE(srInit());
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        runtimeImages();
        REQUIRE((fs::exists(user / "capture.jpeg") && !fs::exists(assets / "capture.jpeg")));
        srExit();
        REQUIRE(srInit());
    }
    {
        for (const char* name :
             {"C:/DATA/loose.jpeg", "D:/Data/disc.jpg", "E:/data/DISC.JPG", "F:/data/disc.jpg"}) {
            auto stream = wiz8::open_file(name);
            auto surface = srImage::load(name, *stream);
            checkGray(surface.get());
            stream->seek(0, wiz8::SeekOrigin::begin);
            unsigned char byte = 0;
            stream->read_exact(&byte, 1);
            REQUIRE((stream->is_open() && byte == 0xff));
        }
        auto first = wiz8::open_file("C:/DATA/archive.jpg"),
             second = wiz8::open_file("Data\\ARCHIVE.JPG");
        first->seek(9, wiz8::SeekOrigin::begin);
        srColorSurfaceIFace::SurfaceDesc desc;
        REQUIRE((srImage::describe(desc, "archive.jpg", *first) && first->tell() == 9));
        auto surface = srImage::load("archive.jpg", *second);
        checkGray(surface.get());
        REQUIRE((first->tell() == 9 && first->is_open()));
        auto broken = wiz8::open_file("Data/Broken.jpg");
        broken->seek(7, wiz8::SeekOrigin::begin);
        REQUIRE((!srImage::describe(desc, "broken.jpg", *broken) && broken->tell() == 7));
        REQUIRE((!srImage::load("broken.jpg", *broken) && broken->is_open()));
        {
            auto* io = first->io();
            REQUIRE((io && SDL_GetIOSize(io) == Sint64(jpeg.size())));
            REQUIRE(SDL_SeekIO(io, -1, SDL_IO_SEEK_END) == Sint64(jpeg.size() - 1));
            unsigned char bytes[8];
            REQUIRE((SDL_ReadIO(io, bytes, sizeof(bytes)) == 1 && bytes[0] == 0xd9));
            REQUIRE((SDL_ReadIO(io, bytes, sizeof(bytes)) == 0 &&
                     SDL_GetIOStatus(io) == SDL_IO_STATUS_EOF));
        }
        for (const auto offset :
             {std::numeric_limits<Sint64>::min(), std::numeric_limits<Sint64>::max()}) {
            first->seek(9, wiz8::SeekOrigin::begin);
            auto* io = first->io();
            REQUIRE(SDL_SeekIO(io, offset, SDL_IO_SEEK_CUR) == -1);
            REQUIRE(first->tell() == 9);
        }
        // The bounded SDL backend owns its context independently of the File
        // object's address, so moves preserve the borrowed stream and cursor.
        {
            auto archived = wiz8::open_file("Data/archive.jpg");
            auto* borrowed = archived->io();
            wiz8::File moved(std::move(*archived));
            archived.reset();
            REQUIRE((moved.io() == borrowed && SDL_GetIOSize(borrowed) == Sint64(jpeg.size())));
            REQUIRE((SDL_SeekIO(borrowed, 9, SDL_IO_SEEK_SET) == 9 && moved.tell() == 9));
            unsigned char byte;
            REQUIRE((SDL_ReadIO(borrowed, &byte, 1) == 1 && moved.tell() == 10));
            REQUIRE(SDL_WriteIO(borrowed, &byte, 1) == 0);
        }
        first->close();
        REQUIRE(!srImage::load("archive.jpg", *first));
        REQUIRE(!srImage::describe(desc, "archive.jpg", *first));
        for (auto* file : {first.get(), second.get()}) {
            bool rejected = false;
            try {
                srImage::save("capture.jpg", *file, *surface);
            } catch (const std::exception&) {
                rejected = true;
            }
            REQUIRE(rejected);
        }
        REQUIRE(LoadSurface(nullptr, nullptr) == nullptr);
        checkTga("C:/data/LOOSE.tga", true);
        checkTga("Data/ARCHIVE.TGA", true);
        checkTga("Data/Broken.tga", false);
        write(user / "data" / "Loose.jpeg", {0, 1, 2});
        auto overlay = wiz8::open_file("Data/loose.jpeg");
        REQUIRE((!srImage::load("loose.jpeg", *overlay) && overlay->is_open()));
        checkTga("Data/Broken.tga", false);
    }
    {
        auto archived = wiz8::open_file("Data/archive.jpg");
        fs::resize_file(archived->physical_path(), 0);
        unsigned char byte;
        REQUIRE(SDL_ReadIO(archived->io(), &byte, 1) == 0);
        REQUIRE(SDL_GetIOStatus(archived->io()) == SDL_IO_STATUS_ERROR);
        write(assets / "data" / "DATA.SLF", archive(jpeg, tga));
    }
    srExit();
    checkUninitialized();
    wiz8::clear_asset_archives();
    (void)0;
    fs::remove_all(root);
    puts("Virtual image streams: direct load/save, reinit, C-F, retail case folding, overlays, SLF "
         "bounds, independent cursors and borrowed handles passed");
}
