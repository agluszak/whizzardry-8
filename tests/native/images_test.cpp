#include <catch2/catch_test_macros.hpp>
#include <ios>
#include <wiz8/asset_paths.h>
#include <wiz8/filesystem.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <zlib.h>
#include <zconf.h>
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
#include <variant>

#include "himage.h"
#include "vsurface.h"
#include "vobject.h"
#include "imgfmt.h"
#include "wiz8/slf.h"
#include "temporary_directory.h"
#include "Types.h"


using Bytes = std::vector<UINT8>;
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
    const std::array<UINT16, 6> packed{0, 1, 0x7c00, 0x03e0, 0x001f, 0x8000};
    const std::array<std::array<UINT8, 3>, 6> colors{{
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

static Bytes sti_bitmap(unsigned width, unsigned height, unsigned depth, const Bytes& pixels,
                        bool compressed)
{
    STCIHeader header{};
    memcpy(header.cID, STCI_ID_STRING, STCI_ID_LEN);
    header.usWidth = width;
    header.usHeight = height;
    header.ubDepth = depth;
    header.fFlags = depth == 8 ? STCI_INDEXED : STCI_RGB;
    if (depth == 8)
        header.Indexed.uiNumberOfColours = 256;
    else {
        header.RGB.uiRedMask = gusRedMask;
        header.RGB.uiGreenMask = gusGreenMask;
        header.RGB.uiBlueMask = gusBlueMask;
    }
    header.uiOriginalSize = width * height * (depth / 8);
    Bytes stored = pixels;
    if (compressed) {
        header.fFlags |= STCI_ZLIB_COMPRESSED;
        uLongf size = compressBound(pixels.size());
        stored.resize(size);
        REQUIRE(compress2(stored.data(), &size, pixels.data(), pixels.size(), 9) == Z_OK);
        stored.resize(size);
    }
    header.uiStoredSize = stored.size();
    Bytes bytes(sizeof(header));
    memcpy(bytes.data(), &header, sizeof(header));
    if (depth == 8)
        for (unsigned i = 0; i < 256; ++i)
            bytes.insert(bytes.end(), {UINT8(i), UINT8(i), UINT8(i)});
    bytes.insert(bytes.end(), stored.begin(), stored.end());
    return bytes;
}

static Bytes rgb_pcx(const UINT8* rgb)
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

static Bytes sti_sheet(const std::vector<STCISubImage>& frames, const Bytes& pixels)
{
    auto bytes = sti_bitmap(0, 0, 8, pixels, false);
    STCIHeader header;
    memcpy(&header, bytes.data(), sizeof(header));
    header.fFlags |= STCI_ETRLE_COMPRESSED;
    header.Indexed.usNumberOfSubImages = frames.size();
    memcpy(bytes.data(), &header, sizeof(header));
    if (!frames.empty()) {
        const auto* records = reinterpret_cast<const UINT8*>(frames.data());
        bytes.insert(bytes.begin() + STCI_HEADER_SIZE + STCI_8BIT_PALETTE_SIZE,
                     records, records + frames.size() * sizeof(STCISubImage));
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
    Bytes bytes(reinterpret_cast<const UINT8*>(&header),
                reinterpret_cast<const UINT8*>(&header) + sizeof(header));
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
    const auto* directory = reinterpret_cast<const UINT8*>(entries.data());
    bytes.insert(bytes.end(), directory, directory + entries.size() * sizeof(wiz8::SlfEntry));
    return bytes;
}

static std::optional<BitmapImage> load(const char* path)
{
    auto image = LoadImage(path);
    auto* bitmap = image ? std::get_if<BitmapImage>(&*image) : nullptr;
    return bitmap ? std::optional<BitmapImage>(std::move(*bitmap)) : std::nullopt;
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

static void check_pcx(const BitmapImage& image)
{
    REQUIRE((image.width == 3 && image.height == 2 && image.bitDepth == 8));
    REQUIRE(image.palette);
    const UINT8 expected[]{0, 1, 1, 2, 3, 0xc1};
    REQUIRE(!memcmp(image.pixels.data(), expected, sizeof(expected)));
    for (unsigned i = 0; i < 256; ++i)
    {
        REQUIRE((*image.palette)[i].peRed == (i == 2 ? 255 : i));
        REQUIRE((*image.palette)[i].peGreen == (i == 2 ? 0 : i));
        REQUIRE((*image.palette)[i].peBlue == (i == 2 ? 0 : i));
        REQUIRE((*image.palette)[i].peFlags == 0);
    }
    const auto palette = Create16BPPPalette(*image.palette);
    REQUIRE(palette[0] == 0); // Exact black stays transparent.
    REQUIRE(palette[1] == 1); // Non-black quantizing to zero is substituted.
    REQUIRE(palette[2] == 0x7c00);
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
        REQUIRE((image && image->pixels.size() == 4 && image->width == 2));
        const UINT8 expected[]{0, 0x7c, 0xe0, 3};
        REQUIRE(!memcmp(image->pixels.data(), expected, sizeof(expected)));
        header.RGB.uiRedMask = 0xf800;
        header.RGB.uiGreenMask = 0x07e0;
        write_rgb();
        image = load("native-rgb.sti");
        REQUIRE((image && image->pixels.size() == 4));
        REQUIRE(image->pixels[1] == 0x3e); // RGB565 red bits become RGB555.
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
        const UINT8 expected[]{0, 1, 1, 2, 3, 0xc1};
        for (int y = 0; y < 2; ++y)
            REQUIRE(!memcmp(static_cast<const UINT8*>(pixels.pixels) + y * pixels.pitch,
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
        REQUIRE((custom && custom->palette));
        const auto palette = Create16BPPPalette(*custom->palette);
        REQUIRE(palette[0] == 0);
        REQUIRE((palette[1] == 0x8001 && palette[2] == 0x801f));
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
        const auto shaded = Create16BPPPaletteShaded(palette, 384, 0, 128, FALSE);
        REQUIRE((shaded[0] == 0 && shaded[255] == 0x0c0f));
        const auto mono = Create16BPPPaletteShaded(palette, 384, 0, 128, TRUE);
        REQUIRE((mono[0] == 0 && mono[255] == 0x4c06));
        const auto clamped = Create16BPPPaletteShaded(palette, 4096, 4096, 4096, FALSE);
        REQUIRE(clamped[255] == 0x7fff);
        gusAlphaMask = 0x8000;
        const auto opaque = Create16BPPPalette(palette);
        REQUIRE((opaque[0] == 0 && opaque[1] == 0x8001 && opaque[2] == 0xfc00));
        rgb555();
    }

    const UINT16 packed[]{0, 1, 0x7c00, 0x03e0, 0x001f, 0x8000};
    const UINT8 rgb[]{0, 0, 0, 1, 1, 1, 255, 0, 0, 0, 255, 0, 0, 0, 255, 23, 45, 67};
    {
        fixture(assets / "rgb.pcx", rgb_pcx(rgb));
        auto image = load("rgb.pcx");
        REQUIRE((image && image->bitDepth == 24 && !image->palette));
        REQUIRE(!memcmp(image->pixels.data(), rgb, sizeof(rgb)));
    }
    for (unsigned depth : {16u, 24u})
        for (bool top : {false, true})
            for (bool rle : {false, true})
            {
                fixture(assets / "test.tga", tga(depth, top, rle));
                auto image = load("test.tga");
                REQUIRE((image && image->width == 3 && image->height == 2));
                REQUIRE((image->bitDepth == depth && !image->palette));
                REQUIRE(!memcmp(image->pixels.data(), depth == 16 ? static_cast<const void*>(packed) : rgb,
                              depth == 16 ? sizeof(packed) : sizeof(rgb)));
            }
    {
        fixture(assets / "indexed.tga", indexed_tga());
        auto image = load("indexed.tga");
        REQUIRE((image && image->bitDepth == 8 && image->palette));
        const UINT8 indices[]{2, 2, 2, 2, 1, 0};
        REQUIRE(!memcmp(image->pixels.data(), indices, sizeof(indices)));
        REQUIRE(((*image->palette)[2].peRed == 255 && (*image->palette)[2].peBlue == 0));
        REQUIRE(((*image->palette)[3].peRed == 0 && (*image->palette)[255].peFlags == 0));
        const auto palette = Create16BPPPalette(*image->palette);
        REQUIRE((palette[0] == 0 && palette[1] == 1));
        auto offset_palette = indexed_tga();
        word(offset_palette, 3, 16);
        offset_palette[28] += 16;
        offset_palette[30] += 16;
        offset_palette[31] += 16;
        fixture(assets / "offset.tga", offset_palette);
        auto offset = load("offset.tga");
        REQUIRE((offset && (*offset->palette)[18].peRed == 255 && (*offset->palette)[0].peRed == 0));
        REQUIRE(((*offset->palette)[17].peRed == 1 && offset->pixels[0] == 18));
        auto override_image = load("override.pcx");
        REQUIRE((override_image && !memcmp(override_image->pixels.data(), indices, sizeof(indices))));
    }
    {
        auto disc_image = load("D:\\DISC.TGA");
        REQUIRE((disc_image && !memcmp(disc_image->pixels.data(), rgb, sizeof(rgb))));
        char entry[] = "Data\\Packed.pcx";
        auto first = [&]() { try { return wiz8::open_file(entry, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
        auto second = [&]() { try { return wiz8::open_file(entry, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
        REQUIRE((first && second && (first->seek(9, wiz8::SeekOrigin::begin), true)));
        auto image = load("C:\\data\\PACKED.PCX");
        REQUIRE(image);
        check_pcx(*image);
        REQUIRE((first->tell() == 9 && second->tell() == 0));
        auto tga_image = load("data\\packed.tga");
        REQUIRE((tga_image && !memcmp(tga_image->pixels.data(), rgb, sizeof(rgb))));
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
                auto* pixel = static_cast<UINT8*>(surface->pixels) + y * surface->pitch + x * 3;
                pixel[0] = 240;
                pixel[1] = 20;
                pixel[2] = 40;
            }
        REQUIRE(IMG_SaveJPG(surface.get(), wiz8::path_to_utf8(assets / "test.JpEg").c_str(), 100));
        auto image = load("test.jpeg");
        REQUIRE((image && image->width == 8 && image->height == 8 && image->bitDepth == 24));
        REQUIRE_FALSE(image->palette);
        for (unsigned i = 0; i < 64; ++i)
        {
            CHECK(abs(int(image->pixels[i * 3]) - 240) < 4);
            CHECK(abs(int(image->pixels[i * 3 + 1]) - 20) < 4);
            CHECK(abs(int(image->pixels[i * 3 + 2]) - 40) < 4);
        }
    }


}

TEST_CASE_METHOD(ImageFixture, "STI bitmaps decode before pitched surface initialization", "[image][decoding]")
{
    for (unsigned depth : {8u, 16u, 24u}) {
        const unsigned width = 5, height = 3;
        Bytes expected(width * height * (depth / 8));
        for (std::size_t i = 0; i < expected.size(); ++i)
            expected[i] = (i * 7) ^ (i >> 5);
        for (bool compressed : {false, true}) {
            fixture(assets / "bitmap.sti", sti_bitmap(width, height, depth, expected, compressed));
            auto image = load("bitmap.sti");
            REQUIRE(image);
            REQUIRE_FALSE(LoadVideoObject("bitmap.sti"));
            REQUIRE(image->pixels == expected);
            REQUIRE(image->palette.has_value() == (depth == 8));
            if (depth == 24) {
                REQUIRE_FALSE(LoadVideoSurface("bitmap.sti")); // SGP CPU surfaces are 8 or 16 bpp.
                continue;
            }
            for (unsigned repeat = 0; repeat < 3; ++repeat) {
                auto surface = LoadVideoSurface("bitmap.sti");
                REQUIRE(surface);
                const auto& storage = *surface->cpu().surface;
                const unsigned row_bytes = width * (depth / 8);
                REQUIRE(storage.pitch > int(row_bytes)); // Odd widths exercise SDL's padded rows.
                for (unsigned row = 0; row < height; ++row) {
                    const auto* pixels = static_cast<const UINT8*>(storage.pixels) + row * storage.pitch;
                    REQUIRE(!memcmp(pixels, expected.data() + row * row_bytes, row_bytes));
                    for (unsigned padding = row_bytes; padding < unsigned(storage.pitch); ++padding)
                        REQUIRE(pixels[padding] == 0);
                }
                REQUIRE(surface->cpu().sourceKey);
                REQUIRE(surface->cpu().sourceKey->low == 0);
                if (depth == 8) {
                    const auto* palette = SDL_GetSurfacePalette(surface->cpu().surface.get());
                    REQUIRE(palette);
                    for (unsigned i = 0; i < 256; ++i) {
                        REQUIRE(palette->colors[i].r == i);
                        REQUIRE(palette->colors[i].g == i);
                        REQUIRE(palette->colors[i].b == i);
                        REQUIRE(palette->colors[i].a == 255);
                    }
                    REQUIRE(surface->p16BPPPalette == Create16BPPPalette(*image->palette));
                }
            }
        }
    }
    // Decoding no longer captures the renderer's palette state.
    const Bytes indices{0, 1, 2, 3, 4, 5};
    fixture(assets / "palette.sti", sti_bitmap(3, 2, 8, indices, true));
    auto image = load("palette.sti");
    REQUIRE((image && image->palette));
    const auto colors = *image->palette;
    gusAlphaMask = 0x8000;
    auto surface = LoadVideoSurface("palette.sti");
    REQUIRE(surface);
    REQUIRE(!memcmp(&*image->palette, &colors, sizeof(colors)));
    REQUIRE(surface->p16BPPPalette[0] == 0);
    REQUIRE(surface->p16BPPPalette[1] == 0x8001);
}

TEST_CASE_METHOD(ImageFixture, "Raw and zlib STI RGB masks normalize identically", "[image][decoding]")
{
    const Bytes source{0, 0, 0, 0xf8, 0xe0, 0x07, 0x1f, 0};
    for (bool compressed : {false, true}) {
        auto bytes = sti_bitmap(2, 2, 16, source, compressed);
        STCIHeader header;
        memcpy(&header, bytes.data(), sizeof(header));
        header.RGB.uiRedMask = 0xf800;
        header.RGB.uiGreenMask = 0x07e0;
        memcpy(bytes.data(), &header, sizeof(header));
        fixture(assets / "rgb565.sti", bytes);
        for (UINT16 alpha : {UINT16(0), UINT16(0x8000)}) {
            gusAlphaMask = alpha;
            auto image = load("rgb565.sti");
            REQUIRE(image);
            const std::array<UINT16, 4> expected{0, UINT16(0x7c00 | alpha),
                                               UINT16(0x03e0 | alpha), UINT16(0x001f | alpha)};
            REQUIRE(image->pixels.size() == sizeof(expected));
            REQUIRE(!memcmp(image->pixels.data(), expected.data(), sizeof(expected)));
            auto surface = LoadVideoSurface("rgb565.sti");
            REQUIRE(surface);
            const auto& storage = *surface->cpu().surface;
            for (int row = 0; row < 2; ++row)
                REQUIRE(!memcmp(static_cast<const UINT8*>(storage.pixels) + row * storage.pitch,
                                expected.data() + row * 2, 4));
        }
    }
}

TEST_CASE_METHOD(ImageFixture, "STI sprite sheets decode and transfer ownership at loading", "[image][decoding]")
{
    const Bytes runs{1, 0, 0x81, 1, 2, 0, 0x82, 1, 3, 0};
    const std::vector<STCISubImage> frames{{0, 10, -2, 3, 2, 3}, {6, 4, 4, -1, 1, 3}};
    const auto bytes = sti_sheet(frames, runs);
    fixture(assets / "sheet.sti", bytes);
    auto image = LoadImage("sheet.sti");
    REQUIRE(image);
    auto* sheet = std::get_if<SpriteSheet>(&*image);
    REQUIRE(sheet);
    REQUIRE(sheet->frames.size() == 2);
    const std::vector<UINT16> expected{0, SpriteFrame::kTransparent, 2,
                                      SpriteFrame::kTransparent, SpriteFrame::kTransparent, 3};
    REQUIRE(sheet->frames[0].pixels == expected);
    REQUIRE((sheet->frames[0].sOffsetX == -2 && sheet->frames[0].sOffsetY == 3));
    REQUIRE((sheet->frames[1].sOffsetX == 4 && sheet->frames[1].sOffsetY == -1));
    REQUIRE(sheet->palette[2].peRed == 2);
    REQUIRE_FALSE(LoadVideoSurface("sheet.sti"));
    const auto* pixels = sheet->frames[0].pixels.data();
    SGPVObject moved(std::move(*sheet));
    REQUIRE(moved.sprites[0].pixels.data() == pixels);
    REQUIRE(moved.sprites[0].pixels == expected);
    auto object = LoadVideoObject("sheet.sti");
    REQUIRE(object);
    image.reset();
    REQUIRE(object->sprites[0].pixels == expected);
    REQUIRE(object->currentPalette() == Create16BPPPalette(object->pPaletteEntry));

    fixture(assets / "Data" / "Sprites.slf", slf({{"Sheet.sti", bytes}}));
    wiz8::mount_slf("Data\\Sprites.slf");
    auto archived = LoadVideoObject("Data\\Sheet.sti");
    REQUIRE(archived);
    REQUIRE(archived->sprites[0].pixels == expected);

    fixture(assets / "empty.sti", sti_sheet({}, {}));
    auto empty = LoadImage("empty.sti");
    REQUIRE(empty);
    REQUIRE(std::holds_alternative<SpriteSheet>(*empty));
    REQUIRE(LoadVideoObject("empty.sti"));
    REQUIRE_FALSE(LoadVideoSurface("empty.sti"));
}

TEST_CASE_METHOD(ImageFixture, "Malformed ETRLE fails before any image is exposed", "[image][validation]")
{
    const Bytes runs{1, 0, 0x81, 1, 2, 0, 0x82, 1, 3, 0};
    const std::vector<STCISubImage> valid_frames{{0, 10, -2, 3, 2, 3}, {6, 4, 4, -1, 1, 3}};
    for (unsigned corruption = 0; corruption < 8; ++corruption) {
        auto frames = valid_frames;
        auto pixels = runs;
        if (corruption == 0) pixels[6] = 0x80; // Zero-length run in the second frame.
        if (corruption == 1) frames[1].uiDataLength = 3; // Missing terminator.
        if (corruption == 2) frames[1].uiDataOffset = 0xffffffffu;
        if (corruption == 3) frames[1].usWidth = 2; // Run exceeds row width.
        if (corruption == 4) frames[1].usWidth = 4; // Short row.
        if (corruption == 5) frames[1].usWidth = frames[1].usHeight = 65535;
        if (corruption == 6) pixels[6] = 3; // Truncated literal run.
        if (corruption == 7) {
            frames[1].usWidth = frames[1].usHeight = 0;
            frames[1].uiDataOffset = pixels.size() + 1;
        }
        fixture(assets / "bad-sheet.sti", sti_sheet(frames, pixels));
        REQUIRE_FALSE(LoadImage("bad-sheet.sti"));
        REQUIRE_FALSE(LoadVideoObject("bad-sheet.sti"));
        fixture(assets / "good-sheet.sti", sti_sheet(valid_frames, runs));
        auto good = LoadVideoObject("good-sheet.sti");
        REQUIRE(good);
        REQUIRE(good->sprites.size() == 2);
        REQUIRE(good->sprites[0].pixels[0] == 0);
    }
}

TEST_CASE_METHOD(ImageFixture, "STI zlib decoding respects SLF entry bounds and independent cursors", "[image][decoding]")
{
    Bytes expected(500 * 200);
    for (std::size_t i = 0; i < expected.size(); ++i)
        expected[i] = (i * 7) ^ (i >> 5);
    const auto valid = sti_bitmap(500, 200, 8, expected, true);
    auto truncated = valid;
    truncated.pop_back();
    fixture(assets / "Data" / "Compressed.slf", slf({{"Good.sti", valid}, {"Bad.sti", truncated},
                                                       {"Following.sti", valid}}));
    wiz8::mount_slf("Data\\Compressed.slf");
    auto first = wiz8::open_file("Data\\Good.sti");
    auto second = wiz8::open_file("Data\\Good.sti");
    first->seek(9, wiz8::SeekOrigin::begin);
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        auto image = load("Data\\Good.sti");
        REQUIRE(image);
        REQUIRE(image->pixels == expected);
        REQUIRE_FALSE(load("Data\\Bad.sti"));
        REQUIRE_FALSE(LoadVideoSurface("Data\\Bad.sti"));
        auto surface = LoadVideoSurface("Data\\Following.sti");
        REQUIRE(surface);
        const auto& storage = *surface->cpu().surface;
        for (int row = 0; row < 200; ++row)
            REQUIRE(!memcmp(static_cast<const UINT8*>(storage.pixels) + row * storage.pitch,
                            expected.data() + row * 500, 500));
        REQUIRE(first->tell() == 9);
        REQUIRE(second->tell() == 0);
    }
}

TEST_CASE_METHOD(ImageFixture, "Malformed STI zlib data fails at the loading boundary", "[image][validation]")
{
    const Bytes original{0, 1, 2, 3, 4, 5};
    const auto valid = sti_bitmap(3, 2, 8, original, true);
    for (unsigned corruption = 0; corruption < 15; ++corruption) {
        auto bytes = valid;
        STCIHeader header;
        memcpy(&header, bytes.data(), sizeof(header));
        if (corruption == 0) bytes.pop_back();
        if (corruption == 1) bytes.back() ^= 1;
        if (corruption == 2) { bytes.push_back(0); ++header.uiStoredSize; }
        if (corruption == 3) ++header.uiOriginalSize;
        if (corruption == 4) ++header.usHeight;
        if (corruption == 5) header.uiOriginalSize = 0xffffffffu;
        if (corruption == 6) header.usWidth = 0;
        if (corruption == 7) header.usHeight = 0;
        if (corruption == 8) header.usWidth = header.usHeight = 65535;
        if (corruption == 9) header.fFlags |= STCI_ETRLE_COMPRESSED;
        if (corruption == 10) header.Indexed.uiNumberOfColours = 255;
        if (corruption == 11) header.uiAppDataSize = 1;
        if (corruption < 12)
            memcpy(bytes.data(), &header, sizeof(header));
        if (corruption == 12) bytes = sti_bitmap(3, 2, 8, {}, true);
        if (corruption == 13) bytes = sti_bitmap(3, 2, 8, {0, 1, 2, 3, 4}, true);
        if (corruption == 14) bytes = sti_bitmap(3, 2, 8, {0, 1, 2, 3, 4, 5, 6}, true);
        fixture(assets / "bad-zlib.sti", bytes);
        for (unsigned repeat = 0; repeat < 3; ++repeat) {
            REQUIRE_FALSE(LoadImage("bad-zlib.sti"));
            REQUIRE_FALSE(LoadVideoSurface("bad-zlib.sti"));
        }
        fixture(assets / "good-zlib.sti", valid);
        auto image = load("good-zlib.sti");
        REQUIRE(image);
        REQUIRE(image->pixels == original);
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
        REQUIRE((image && image->bitDepth == 24 && !image->palette));
        REQUIRE_FALSE(image->palette);
    }
    fixture(assets / "invalid.jpg", {0xff, 0xd8, 0xff});
    REQUIRE((!load("invalid.jpg") && !load("missing.pcx") && !load("unsupported.png")));
    fixture(assets / "oversized.jpg", {0xff, 0xd8, 0xff, 0xc0, 0, 17, 8, 0xff, 0xff,
        0xff, 0xff, 3, 1, 0x11, 0, 2, 0x11, 1, 3, 0x11, 1});
    REQUIRE(!load("oversized.jpg"));
    REQUIRE(!load(wiz8::path_to_utf8(assets / "test.tga").c_str()));
    const std::string long_path(SGPFILENAME_LEN + 10, 'x');
    REQUIRE(!load(long_path.c_str()));
    REQUIRE(!LoadImage(""));


}
