#include <cstdint>
#include <catch2/catch_test_macros.hpp>
#include <ios>
#include <wiz8/asset_paths.h>
#include <wiz8/filesystem.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <cstddef>
#include <exception>
#include <utility>

#include "himage.h"
#include "vsurface.h"
#include "imgfmt.h"
#include "wiz8/slf.h"
#include "temporary_directory.h"


using Bytes = std::vector<std::uint8_t>;
using Image = std::optional<image_type>;
namespace fs = std::filesystem;

static void word(Bytes& bytes, std::size_t offset, unsigned value)
{
    bytes[offset] = value;
    bytes[offset + 1] = value >> 8;
}

static void fixture(const fs::path& path, const Bytes& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    REQUIRE(file.good());
}

static Bytes pcx()
{
    Bytes bytes(128);
    bytes[0] = 10;
    bytes[1] = 5;
    bytes[2] = 1;
    bytes[3] = 8;
    word(bytes, 8, 2); // Width 3, with one encoded padding byte per row.
    word(bytes, 10, 1);
    bytes[65] = 1;
    word(bytes, 66, 4);
    bytes.insert(bytes.end(), {0, 0xc2, 1, 99, 2, 3, 0xc1, 0xc1, 99, 12});
    for (unsigned i = 0; i < 256; ++i)
    {
        bytes.push_back(i == 2 ? 255 : i == 0 ? 0 : i);
        bytes.push_back(i == 2 ? 0 : i == 0 ? 0 : i);
        bytes.push_back(i == 2 ? 0 : i == 0 ? 0 : i);
    }
    return bytes;
}

static Bytes tga(unsigned depth, bool top, bool rle)
{
    Bytes bytes(18);
    bytes[2] = rle ? 10 : 2;
    word(bytes, 12, 3);
    word(bytes, 14, 2);
    bytes[16] = depth;
    bytes[17] = top ? 0x20 : 0;
    const std::array<std::uint16_t, 6> packed{0, 1, 0x7c00, 0x03e0, 0x001f, 0x8000};
    const std::array<std::array<std::uint8_t, 3>, 6> colors{{
        {0, 0, 0}, {1, 1, 1}, {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {23, 45, 67}}};
    if (rle)
        bytes.push_back(5); // One raw RLE packet crossing the scan-line boundary.
    for (unsigned row = 0; row < 2; ++row)
        for (unsigned x = 0; x < 3; ++x)
        {
            const unsigned index = (top ? row : 1 - row) * 3 + x;
            if (depth == 16)
            {
                bytes.push_back(packed[index]);
                bytes.push_back(packed[index] >> 8);
            }
            else
                bytes.insert(bytes.end(), {colors[index][2], colors[index][1], colors[index][0]});
        }
    return bytes;
}

static Bytes indexed_tga()
{
    Bytes bytes(18);
    bytes[1] = 1;
    bytes[2] = 9;
    word(bytes, 5, 3);
    bytes[7] = 24;
    word(bytes, 12, 3);
    word(bytes, 14, 2);
    bytes[16] = 8;
    bytes[17] = 0x20;
    bytes.insert(bytes.end(), {0, 0, 0, 1, 1, 1, 0, 0, 255,
                              0x83, 2, 1, 1, 0}); // A repeated run across rows, then raw pixels.
    return bytes;
}

static Bytes rgb_pcx(const std::uint8_t* rgb)
{
    auto bytes = pcx();
    bytes.resize(128);
    bytes[65] = 3;
    for (unsigned row = 0; row < 2; ++row)
        for (unsigned plane = 0; plane < 3; ++plane)
        {
            for (unsigned x = 0; x < 3; ++x)
            {
                const auto value = rgb[(row * 3 + x) * 3 + plane];
                if (value >= 0xc0)
                    bytes.push_back(0xc1);
                bytes.push_back(value);
            }
            bytes.push_back(99);
        }
    return bytes;
}

static Bytes slf(const std::vector<std::pair<std::string, Bytes>>& files)
{
    static_assert(sizeof(wiz8::SlfHeader) == 532 && sizeof(wiz8::SlfEntry) == 280);
    wiz8::SlfHeader header{};
    strcpy(header.sLibName, "Data.slf");
    strcpy(header.sPathToLibrary, "Data\\");
    header.iEntries = header.iUsed = files.size();
    header.iVersion = 0x200;
    Bytes bytes(reinterpret_cast<const std::uint8_t*>(&header),
                reinterpret_cast<const std::uint8_t*>(&header) + sizeof(header));
    std::vector<wiz8::SlfEntry> entries;
    for (const auto& [name, data] : files)
    {
        wiz8::SlfEntry entry{};
        REQUIRE(name.size() < sizeof(entry.sFileName));
        memcpy(entry.sFileName, name.c_str(), name.size() + 1);
        entry.uiOffset = bytes.size();
        entry.uiLength = data.size();
        entries.push_back(entry);
        bytes.insert(bytes.end(), data.begin(), data.end());
    }
    const auto* directory = reinterpret_cast<const std::uint8_t*>(entries.data());
    bytes.insert(bytes.end(), directory, directory + entries.size() * sizeof(wiz8::SlfEntry));
    return bytes;
}

static Image load(const char* path)
{
    return LoadImage(path);
}

static void rgb555()
{
    gusAlphaMask = 0;
    gusRedMask = 0x7c00;
    gusGreenMask = 0x03e0;
    gusBlueMask = 0x001f;
    gusRedShift = 7;
    gusGreenShift = 2;
    gusBlueShift = -3;
}

static void check_pcx(const image_type& image)
{
    REQUIRE((image.usWidth == 3 && image.usHeight == 2 && image.ubBitDepth == 8));
    REQUIRE((image.pPalette && !image.etrle && !image.compressed));
    const std::uint8_t expected[]{0, 1, 1, 2, 3, 0xc1};
    REQUIRE(!memcmp(image.pImageData.data(), expected, sizeof(expected)));
    for (unsigned i = 0; i < 256; ++i)
    {
        REQUIRE((*image.pPalette)[i].peRed == (i == 2 ? 255 : i));
        REQUIRE((*image.pPalette)[i].peGreen == (i == 2 ? 0 : i));
        REQUIRE((*image.pPalette)[i].peBlue == (i == 2 ? 0 : i));
        REQUIRE((*image.pPalette)[i].peFlags == 0);
    }
    REQUIRE((*image.pui16BPPPalette)[0] == 0); // Exact black stays transparent.
    REQUIRE((*image.pui16BPPPalette)[1] == 1); // Non-black quantizing to zero is substituted.
    REQUIRE((*image.pui16BPPPalette)[2] == 0x7c00);
    REQUIRE(image.pETRLEObject.empty());
}

struct ImageFixture
{
    ScopedTemporaryDirectory directory{"wiz8-images"};
    fs::path root = directory.path;
    fs::path assets = root / "assets", user = root / "user", disc = root / "disc";
    Bytes truncated;

    ImageFixture()
    {
        fs::create_directories(user);
        fixture(assets / "Dotted.dir" / "many.dots.PcX", pcx());
        fixture(assets / "Dotted.dir" / "default.PCX", pcx());
        fixture(assets / "override.pcx", pcx());
        auto override_pcx = pcx();
        override_pcx[128] = override_pcx[130] = override_pcx[132] = 2;
        override_pcx[133] = 1;
        override_pcx[135] = 0;
        fixture(user / "override.pcx", override_pcx);
        fixture(disc / "disc.tga", tga(24, true, false));
        truncated = tga(24, true, false);
        truncated.resize(21);
        fixture(assets / "Data" / "Data.slf", slf({{"Packed.pcx", pcx()},
            {"Packed.tga", tga(24, false, true)}, {"Truncated.tga", truncated}}));
        w8_native::configure_paths({wiz8::path_to_utf8(assets), wiz8::path_to_utf8(user),
                                    {wiz8::path_to_utf8(disc), "", ""}});

        wiz8::mount_slf("Data\\Data.slf");
        rgb555();
    }

    ~ImageFixture()
    {
        wiz8::clear_asset_archives();
        rgb555();
    }
};

TEST_CASE_METHOD(ImageFixture, "Native image formats, palettes and archives", "[image][decoding]")
{
    {
        static_assert(sizeof(STCIHeader) == 64);
        static_assert(offsetof(STCIHeader, uiAppDataSize) == 48);
        STCIHeader header{};
        memcpy(header.cID, STCI_ID_STRING, STCI_ID_LEN);
        header.fFlags = STCI_RGB;
        header.usWidth = 2;
        header.usHeight = 1;
        header.ubDepth = 16;
        header.RGB.uiRedMask = gusRedMask;
        header.RGB.uiGreenMask = gusGreenMask;
        header.RGB.uiBlueMask = gusBlueMask;
        header.uiStoredSize = header.uiOriginalSize = 4;
        const auto write_rgb = [&] {
            Bytes bytes(STCI_HEADER_SIZE + 4);
            memcpy(bytes.data(), &header, STCI_HEADER_SIZE);
            word(bytes, STCI_HEADER_SIZE, 0x7c00);
            word(bytes, STCI_HEADER_SIZE + 2, 0x03e0);
            fixture(assets / "native-rgb.sti", bytes);
        };
        write_rgb();
        auto image = load("native-rgb.sti");
        REQUIRE((image && image->pImageData.size() == 4 && image->usWidth == 2));
        const std::uint8_t expected[]{0, 0x7c, 0xe0, 3};
        REQUIRE(!memcmp(image->pImageData.data(), expected, sizeof(expected)));
        header.RGB.uiRedMask = 0xf800;
        header.RGB.uiGreenMask = 0x07e0;
        write_rgb();
        image = load("native-rgb.sti");
        REQUIRE((image && image->pImageData.size() == 4));
        REQUIRE(image->pImageData[1] == 0x3e); // RGB565 red bits become RGB555.
        header.uiStoredSize = 1;
        write_rgb();
        REQUIRE(!load("native-rgb.sti"));
        header.uiStoredSize = 0xffffffffu;
        write_rgb();
        REQUIRE(!load("native-rgb.sti"));
    }


    {
        auto image = load("c:\\dotted.DIR\\many.dots.pcx");
        REQUIRE(image);
        check_pcx(*image);
        char extensionless[] = "Dotted.dir\\default";
        auto default_image = load(extensionless);
        REQUIRE((default_image && !strcmp(extensionless, "Dotted.dir\\default")));
        check_pcx(*default_image);

        // The surface factory must copy the image pixels, not merely allocate storage.
        auto surface = LoadVideoSurface("Dotted.dir/default.PCX");
        REQUIRE(surface);
        const auto& pixels = *surface->cpu().surface;
        REQUIRE((pixels.w == 3 && pixels.h == 2 && surface->ubBitDepth == 8));
        const std::uint8_t expected[]{0, 1, 1, 2, 3, 0xc1};
        for (int y = 0; y < 2; ++y)
            REQUIRE(!memcmp(static_cast<const std::uint8_t*>(pixels.pixels) + y * pixels.pitch,
                            expected + y * 3, 3));
    }
    {
        auto complete = load("Dotted.dir/default.PCX");
        REQUIRE(complete);
        check_pcx(*complete);
        gusAlphaMask = 0x8000;
        gusRedMask = 0x001f;
        gusBlueMask = 0x7c00;
        gusRedShift = -3;
        gusBlueShift = 7;
        auto custom = load("Dotted.dir/default.PCX");
        REQUIRE((custom && (*custom->pui16BPPPalette)[0] == 0));
        REQUIRE(((*custom->pui16BPPPalette)[1] == 0x8001 && (*custom->pui16BPPPalette)[2] == 0x801f));
        rgb555();
    }

    {
        std::array<SGPPaletteEntry, 256> palette{};
        palette[0] = {0, 0, 0, 255};
        palette[1] = {1, 1, 1, 255};
        palette[2] = {255, 0, 0, 0};
        palette[255] = {17, 121, 244, 0};
        const auto packed_palette = Create16BPPPalette(palette);
        REQUIRE((packed_palette[0] == 0 && packed_palette[1] == 1 && packed_palette[2] == 0x7c00));
        const auto shaded = Create16BPPPaletteShaded(palette, 384, 0, 128, false);
        REQUIRE((shaded[0] == 0 && shaded[255] == 0x0c0f));
        const auto mono = Create16BPPPaletteShaded(palette, 384, 0, 128, true);
        REQUIRE((mono[0] == 0 && mono[255] == 0x4c06));
        const auto clamped = Create16BPPPaletteShaded(palette, 4096, 4096, 4096, false);
        REQUIRE(clamped[255] == 0x7fff);
        gusAlphaMask = 0x8000;
        const auto opaque = Create16BPPPalette(palette);
        REQUIRE((opaque[0] == 0 && opaque[1] == 0x8001 && opaque[2] == 0xfc00));
        rgb555();
    }

    const std::uint16_t packed[]{0, 1, 0x7c00, 0x03e0, 0x001f, 0x8000};
    const std::uint8_t rgb[]{0, 0, 0, 1, 1, 1, 255, 0, 0, 0, 255, 0, 0, 0, 255, 23, 45, 67};
    {
        fixture(assets / "rgb.pcx", rgb_pcx(rgb));
        auto image = load("rgb.pcx");
        REQUIRE((image && image->ubBitDepth == 24 && !image->pPalette));
        REQUIRE(!memcmp(image->pImageData.data(), rgb, sizeof(rgb)));
    }
    for (unsigned depth : {16u, 24u})
        for (bool top : {false, true})
            for (bool rle : {false, true})
            {
                fixture(assets / "test.tga", tga(depth, top, rle));
                auto image = load("test.tga");
                REQUIRE((image && image->usWidth == 3 && image->usHeight == 2));
                REQUIRE((image->ubBitDepth == depth && !image->pPalette));
                REQUIRE((!image->pPalette && !image->pui16BPPPalette));
                REQUIRE(!memcmp(image->pImageData.data(), depth == 16 ? static_cast<const void*>(packed) : rgb,
                              depth == 16 ? sizeof(packed) : sizeof(rgb)));
            }
    {
        fixture(assets / "indexed.tga", indexed_tga());
        auto image = load("indexed.tga");
        REQUIRE((image && image->ubBitDepth == 8 && image->pPalette));
        const std::uint8_t indices[]{2, 2, 2, 2, 1, 0};
        REQUIRE(!memcmp(image->pImageData.data(), indices, sizeof(indices)));
        REQUIRE(((*image->pPalette)[2].peRed == 255 && (*image->pPalette)[2].peBlue == 0));
        REQUIRE(((*image->pPalette)[3].peRed == 0 && (*image->pPalette)[255].peFlags == 0));
        REQUIRE(((*image->pui16BPPPalette)[0] == 0 && (*image->pui16BPPPalette)[1] == 1));
        auto offset_palette = indexed_tga();
        word(offset_palette, 3, 16);
        offset_palette[28] += 16;
        offset_palette[30] += 16;
        offset_palette[31] += 16;
        fixture(assets / "offset.tga", offset_palette);
        auto offset = load("offset.tga");
        REQUIRE((offset && (*offset->pPalette)[18].peRed == 255 && (*offset->pPalette)[0].peRed == 0));
        REQUIRE(((*offset->pPalette)[17].peRed == 1 && offset->pImageData[0] == 18));
        auto override_image = load("override.pcx");
        REQUIRE((override_image && !memcmp(override_image->pImageData.data(), indices, sizeof(indices))));
    }
    {
        auto disc_image = load("D:\\DISC.TGA");
        REQUIRE((disc_image && !memcmp(disc_image->pImageData.data(), rgb, sizeof(rgb))));
        char entry[] = "Data\\Packed.pcx";
        auto first = [&]() { try { return wiz8::open_file(entry, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
        auto second = [&]() { try { return wiz8::open_file(entry, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
        REQUIRE((first && second && (first->seek(9, wiz8::SeekOrigin::begin), true)));
        auto image = load("C:\\data\\PACKED.PCX");
        REQUIRE(image);
        check_pcx(*image);
        REQUIRE((first->tell() == 9 && second->tell() == 0));
        auto tga_image = load("data\\packed.tga");
        REQUIRE((tga_image && !memcmp(tga_image->pImageData.data(), rgb, sizeof(rgb))));
        REQUIRE(!load("Data\\Truncated.tga")); // Must not read the next SLF record as image data.
        REQUIRE((first->tell() == 9 && second->tell() == 0));
        first.reset();
        second.reset();
    }
    {
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
            SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGB24), SDL_DestroySurface);
        REQUIRE(surface);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
            {
                auto* pixel = static_cast<std::uint8_t*>(surface->pixels) + y * surface->pitch + x * 3;
                pixel[0] = 240;
                pixel[1] = 20;
                pixel[2] = 40;
            }
        REQUIRE(IMG_SaveJPG(surface.get(), wiz8::path_to_utf8(assets / "test.JpEg").c_str(), 100));
        auto image = load("test.jpeg");
        REQUIRE((image && image->usWidth == 8 && image->usHeight == 8 && image->ubBitDepth == 24));
        REQUIRE((!image->pPalette && !image->etrle && !image->compressed));
        for (unsigned i = 0; i < 64; ++i)
        {
            CHECK(abs(int(image->pImageData[i * 3]) - 240) < 4);
            CHECK(abs(int(image->pImageData[i * 3 + 1]) - 20) < 4);
            CHECK(abs(int(image->pImageData[i * 3 + 2]) - 40) < 4);
        }
    }


}

TEST_CASE_METHOD(ImageFixture, "Truncated images and rollback", "[image][validation]")
{
    fixture(assets / "test.tga", tga(24, true, false));
    std::vector<Bytes> invalid_pcx{{}, Bytes(127), pcx(), pcx(), pcx(), pcx()};
    invalid_pcx[2][0] = 0;
    word(invalid_pcx[3], 4, 10); // Reversed extent.
    word(invalid_pcx[4], 66, 2); // Short row stride.
    invalid_pcx[5].resize(130); // Truncated RLE/palette.
    for (const auto& bytes : invalid_pcx)
    {
        fixture(assets / "invalid.pcx", bytes);
        for (unsigned repeat = 0; repeat < 10; ++repeat)
            CHECK(!load("invalid.pcx"));
    }
    std::vector<Bytes> invalid_tga{{}, Bytes(17), truncated, tga(24, true, true),
                                  tga(24, true, false), tga(24, true, false), tga(24, true, false)};
    invalid_tga[3].resize(19); // Missing RLE payload.
    word(invalid_tga[4], 12, 0);
    word(invalid_tga[5], 12, 65535);
    word(invalid_tga[5], 14, 65535); // Rejected before SDL's decoded allocation.
    invalid_tga[6][2] = 7;
    for (const auto& bytes : invalid_tga)
    {
        fixture(assets / "invalid.tga", bytes);
        REQUIRE(!load("invalid.tga"));
    }
    {
        // Failed loads do not publish partially decoded images.
        REQUIRE(!load("invalid.pcx"));
        REQUIRE(!load("invalid.tga"));
        auto image = load("test.tga");
        REQUIRE((image && image->ubBitDepth == 24 && !image->pPalette && !image->pui16BPPPalette));
        REQUIRE((!image->pPalette && image->pETRLEObject.empty()));
    }
    fixture(assets / "invalid.jpg", {0xff, 0xd8, 0xff});
    REQUIRE((!load("invalid.jpg") && !load("missing.pcx") && !load("unsupported.png")));
    fixture(assets / "oversized.jpg", {0xff, 0xd8, 0xff, 0xc0, 0, 17, 8, 0xff, 0xff,
        0xff, 0xff, 3, 1, 0x11, 0, 2, 0x11, 1, 3, 0x11, 1});
    REQUIRE(!load("oversized.jpg"));
    REQUIRE(!load(wiz8::path_to_utf8(assets / "test.tga").c_str()));
    const std::string long_path(256, 'x');
    REQUIRE(!load(long_path.c_str()));
    REQUIRE(!LoadImage(""));


}
