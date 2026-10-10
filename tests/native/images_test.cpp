#include "himage.h"
#include "imgfmt.h"
#include "wiz8/slf.h"
#include "temporary_directory.h"
#include <wiz8/asset_paths.h>
#include <wiz8/filesystem.h>
#include <SDL3_image/SDL_image.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #expression, SDL_GetError()); \
    std::exit(1); } } while (0)

using Bytes = std::vector<UINT8>;
using Image = std::unique_ptr<image_type>;
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
    CHECK(file.good());
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
        CHECK(name.size() < sizeof(entry.sFileName));
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

static Image load(const char* path, UINT16 contents = IMAGE_ALLDATA)
{
    return CreateImage(path, contents);
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
    CHECK(image.usWidth == 3 && image.usHeight == 2 && image.ubBitDepth == 8);
    CHECK(image.fFlags == IMAGE_ALLIMAGEDATA);
    const UINT8 expected[]{0, 1, 1, 2, 3, 0xc1};
    CHECK(!memcmp(image.pImageData.data(), expected, sizeof(expected)));
    for (unsigned i = 0; i < 256; ++i)
    {
        CHECK(image.pPalette[i].peRed == (i == 2 ? 255 : i));
        CHECK(image.pPalette[i].peGreen == (i == 2 ? 0 : i));
        CHECK(image.pPalette[i].peBlue == (i == 2 ? 0 : i));
        CHECK(image.pPalette[i].peFlags == 0);
    }
    CHECK(image.pui16BPPPalette[0] == 0); // Exact black stays transparent.
    CHECK(image.pui16BPPPalette[1] == 1); // Non-black quantizing to zero is substituted.
    CHECK(image.pui16BPPPalette[2] == 0x7c00);
    CHECK(image.pAppData.empty() && !image.pAppData.size() && !image.pETRLEObject.size());
}

int main() try
{
    const fs::path root = wiz8::path_from_utf8(make_temporary_directory("wiz8-images"));
    const auto assets = root / "assets", user = root / "user", disc = root / "disc";
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
    auto truncated = tga(24, true, false);
    truncated.resize(21);
    fixture(assets / "Data" / "Data.slf", slf({{"Packed.pcx", pcx()},
        {"Packed.tga", tga(24, false, true)}, {"Truncated.tga", truncated}}));
    w8_native::configure_paths({wiz8::path_to_utf8(assets), wiz8::path_to_utf8(user),
                                {wiz8::path_to_utf8(disc), "", ""}});

    wiz8::mount_slf("Data\\Data.slf");
    rgb555();
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
        CHECK(image && image->pImageData.size() == 4 && image->usWidth == 2);
        const UINT8 expected[]{0, 0x7c, 0xe0, 3};
        CHECK(!memcmp(image->pImageData.data(), expected, sizeof(expected)));
        CHECK(load("native-rgb.sti", 0));
        header.RGB.uiRedMask = 0xf800;
        header.RGB.uiGreenMask = 0x07e0;
        write_rgb();
        image = load("native-rgb.sti");
        CHECK(image && image->pImageData.size() == 4);
        CHECK(image->pImageData[1] == 0x3e); // RGB565 red bits become RGB555.
        header.uiStoredSize = 1;
        write_rgb();
        CHECK(!load("native-rgb.sti"));
        header.uiStoredSize = 0xffffffffu;
        write_rgb();
        CHECK(!load("native-rgb.sti"));
    }


    {
        auto image = load("c:\\dotted.DIR\\many.dots.pcx");
        CHECK(image);
        check_pcx(*image);
        char extensionless[] = "Dotted.dir\\default";
        auto default_image = load(extensionless);
        CHECK(default_image && !strcmp(extensionless, "Dotted.dir\\default"));
        check_pcx(*default_image);
        CHECK(default_image->ImageFile == "Dotted.dir\\default.PCX");
    }
    {
        auto palette = load("Dotted.dir/default.PCX", IMAGE_PALETTE);
        auto bitmap = load("Dotted.dir/default.PCX", IMAGE_BITMAPDATA);
        auto metadata = load("Dotted.dir/default.PCX", 0);
        CHECK(palette && bitmap && metadata);
        CHECK(palette->fFlags == IMAGE_PALETTE && palette->pImageData.empty() && palette->pPalette);
        CHECK(bitmap->fFlags == IMAGE_BITMAPDATA && !bitmap->pImageData.empty() && !bitmap->pPalette);
        CHECK(metadata->fFlags == 0 && metadata->pImageData.empty() && !metadata->pPalette);
        CHECK(LoadImageData(metadata.get(), IMAGE_ALLIMAGEDATA));
        check_pcx(*metadata);
        CHECK(LoadImageData(metadata.get(), IMAGE_ALLIMAGEDATA));
        check_pcx(*metadata);
        gusAlphaMask = 0x8000;
        gusRedMask = 0x001f;
        gusBlueMask = 0x7c00;
        gusRedShift = -3;
        gusBlueShift = 7;
        auto custom = load("Dotted.dir/default.PCX");
        CHECK(custom && custom->pui16BPPPalette[0] == 0);
        CHECK(custom->pui16BPPPalette[1] == 0x8001 && custom->pui16BPPPalette[2] == 0x801f);
        rgb555();
    }

    const UINT16 packed[]{0, 1, 0x7c00, 0x03e0, 0x001f, 0x8000};
    const UINT8 rgb[]{0, 0, 0, 1, 1, 1, 255, 0, 0, 0, 255, 0, 0, 0, 255, 23, 45, 67};
    {
        fixture(assets / "rgb.pcx", rgb_pcx(rgb));
        auto image = load("rgb.pcx");
        CHECK(image && image->ubBitDepth == 24 && image->fFlags == IMAGE_BITMAPDATA);
        CHECK(!memcmp(image->pImageData.data(), rgb, sizeof(rgb)));
    }
    for (unsigned depth : {16u, 24u})
        for (bool top : {false, true})
            for (bool rle : {false, true})
            {
                fixture(assets / "test.tga", tga(depth, top, rle));
                auto image = load("test.tga");
                CHECK(image && image->usWidth == 3 && image->usHeight == 2);
                CHECK(image->ubBitDepth == depth && image->fFlags == IMAGE_BITMAPDATA);
                CHECK(!image->pPalette && !image->pui16BPPPalette);
                CHECK(!memcmp(image->pImageData.data(), depth == 16 ? static_cast<const void*>(packed) : rgb,
                              depth == 16 ? sizeof(packed) : sizeof(rgb)));
            }
    {
        fixture(assets / "indexed.tga", indexed_tga());
        auto image = load("indexed.tga");
        CHECK(image && image->ubBitDepth == 8 && image->fFlags == IMAGE_ALLIMAGEDATA);
        const UINT8 indices[]{2, 2, 2, 2, 1, 0};
        CHECK(!memcmp(image->pImageData.data(), indices, sizeof(indices)));
        CHECK(image->pPalette[2].peRed == 255 && image->pPalette[2].peBlue == 0);
        CHECK(image->pPalette[3].peRed == 0 && image->pPalette[255].peFlags == 0);
        CHECK(image->pui16BPPPalette[0] == 0 && image->pui16BPPPalette[1] == 1);
        auto offset_palette = indexed_tga();
        word(offset_palette, 3, 16);
        offset_palette[28] += 16;
        offset_palette[30] += 16;
        offset_palette[31] += 16;
        fixture(assets / "offset.tga", offset_palette);
        auto offset = load("offset.tga");
        CHECK(offset && offset->pPalette[18].peRed == 255 && offset->pPalette[0].peRed == 0);
        CHECK(offset->pPalette[17].peRed == 1 && offset->pImageData[0] == 18);
        auto override_image = load("override.pcx");
        CHECK(override_image && !memcmp(override_image->pImageData.data(), indices, sizeof(indices)));
    }
    {
        auto disc_image = load("D:\\DISC.TGA");
        CHECK(disc_image && !memcmp(disc_image->pImageData.data(), rgb, sizeof(rgb)));
        char entry[] = "Data\\Packed.pcx";
        auto first = [&]() { try { return wiz8::open_file(entry, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
        auto second = [&]() { try { return wiz8::open_file(entry, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
        CHECK(first && second && (first->seek(9, wiz8::SeekOrigin::begin), true));
        auto image = load("C:\\data\\PACKED.PCX");
        CHECK(image);
        check_pcx(*image);
        CHECK(first->tell() == 9 && second->tell() == 0);
        auto tga_image = load("data\\packed.tga");
        CHECK(tga_image && !memcmp(tga_image->pImageData.data(), rgb, sizeof(rgb)));
        CHECK(!load("Data\\Truncated.tga")); // Must not read the next SLF record as image data.
        CHECK(first->tell() == 9 && second->tell() == 0);
        first.reset();
        second.reset();
    }
    {
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
            SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGB24), SDL_DestroySurface);
        CHECK(surface);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
            {
                auto* pixel = static_cast<UINT8*>(surface->pixels) + y * surface->pitch + x * 3;
                pixel[0] = 240;
                pixel[1] = 20;
                pixel[2] = 40;
            }
        CHECK(IMG_SaveJPG(surface.get(), wiz8::path_to_utf8(assets / "test.JpEg").c_str(), 100));
        auto image = load("test.jpeg");
        CHECK(image && image->usWidth == 8 && image->usHeight == 8 && image->ubBitDepth == 24);
        CHECK(image->fFlags == IMAGE_BITMAPDATA);
        for (unsigned i = 0; i < 64; ++i)
        {
            CHECK(abs(int(image->pImageData[i * 3]) - 240) < 4);
            CHECK(abs(int(image->pImageData[i * 3 + 1]) - 20) < 4);
            CHECK(abs(int(image->pImageData[i * 3 + 2]) - 40) < 4);
        }
    }

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
        CHECK(!load("invalid.tga"));
    }
    {
        auto image = load("Dotted.dir/default.PCX");
        CHECK(image);
        auto* data = image->pImageData.data();
        image->ImageFile = "invalid.pcx";
        CHECK(!LoadImageData(image.get(), IMAGE_ALLIMAGEDATA) && image->pImageData.data() == data);
        check_pcx(*image);
        auto* palette = image->pPalette.get();
        image->ImageFile = "invalid.tga";
        CHECK(!LoadImageData(image.get(), IMAGE_ALLDATA));
        CHECK(image->pImageData.data() == data && image->pPalette.get() == palette);
        check_pcx(*image);
        image->ImageFile = "test.tga";
        CHECK(LoadImageData(image.get(), IMAGE_ALLDATA));
        CHECK(image->ubBitDepth == 24 && !image->pPalette && !image->pui16BPPPalette);
        CHECK(image->fFlags == IMAGE_BITMAPDATA && !image->pETRLEObject.size());
    }
    fixture(assets / "invalid.jpg", {0xff, 0xd8, 0xff});
    CHECK(!load("invalid.jpg") && !load("missing.pcx") && !load("unsupported.png"));
    fixture(assets / "oversized.jpg", {0xff, 0xd8, 0xff, 0xc0, 0, 17, 8, 0xff, 0xff,
        0xff, 0xff, 3, 1, 0x11, 0, 2, 0x11, 1, 3, 0x11, 1});
    CHECK(!load("oversized.jpg"));
    CHECK(!load(wiz8::path_to_utf8(assets / "test.tga").c_str()));
    const std::string long_path(SGPFILENAME_LEN + 10, 'x');
    CHECK(!load(long_path.c_str()));
    CHECK(!CreateImage(nullptr, IMAGE_ALLDATA));

    wiz8::clear_asset_archives();
    fs::remove_all(root);
    puts("ok: SDL_image PCX/TGA/JPEG, palette/packed-pixel policy, orientation, bounded SLF and failures");
}
catch (const std::exception& error)
{
    fprintf(stderr, "image fixture: %s\n", error.what());
    return 1;
}
