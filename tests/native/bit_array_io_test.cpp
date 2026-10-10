#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <cstdio>
#include <fstream>
#include <vector>
#include <filesystem>
#include <memory>
#include <string>

#include "wiz8/packed_bits.h"
#include "wiz8/filesystem.h"
#include "temporary_directory.h"


TEST_CASE("packed bit IO")
{
    const auto root = std::filesystem::path(make_temporary_directory("bit-array-io"));
    const auto path = root / "bits.bin";
    const unsigned int words[] = {0x81, 0x12345678, 0};
    std::vector<bool> original(64);
    for (unsigned bit = 0; bit < original.size(); ++bit)
        original[bit] = (words[bit / 32] >> (bit % 32)) & 1;
    {
        auto file = wiz8::open_host_file(path, wiz8::OpenMode::replace);
        REQUIRE((SavePackedBits(file.get(), original)));
        REQUIRE((file->seek(0, wiz8::SeekOrigin::begin) == 0));
        Uint32 magic;
        REQUIRE((SDL_ReadU32LE(file->io(), &magic) && magic == 0xdeadd00d));
        Uint32 count, packed_size;
        REQUIRE((SDL_ReadU32LE(file->io(), &count) && count == 64));
        REQUIRE((SDL_ReadU32LE(file->io(), &packed_size)));
        REQUIRE((packed_size + 16 == file->size()));
        REQUIRE((SDL_SeekIO(file->io(), -4, SDL_IO_SEEK_END) == file->size() - 4));
        REQUIRE((SDL_ReadU32LE(file->io(), &magic) && magic == 0xdeadd00d));
    }
    auto file = wiz8::open_host_file(path);
    std::vector<unsigned char> encoded(file->size());
    file->read_exact(encoded.data(), encoded.size());
    // Captured from BitArray::Save at c8b831f, before the vector migration.
    const std::vector<unsigned char> legacy = {
        0x0d, 0xd0, 0xad, 0xde, 0x40, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00, 0xdd, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
        0xc0, 0x40, 0x00, 0x00, 0x10, 0xcf, 0x8a, 0x46, 0x36, 0x0d, 0xd0, 0xad,
        0xde,
    };
    REQUIRE((encoded == legacy));
    file->seek(0, wiz8::SeekOrigin::begin);
    std::vector<bool> restored(1);
    REQUIRE((LoadPackedBits(file.get(), restored) && restored.size() == 64));
    REQUIRE((restored == original));
    file->close();
    ClearBit(restored, 7);
    REQUIRE((!SetBit(restored, 7) && SetBit(restored, 7)));
    ClearBit(restored, 7);
    REQUIRE((!TestBit(restored, 7)));
    REQUIRE((!SetBit(restored, restored.size()) && !TestBit(restored, restored.size())));
    // Missing framing and Huffman bytes fail without changing the destination.
    for (const auto size : {size_t(0), size_t(12), encoded.size() - 4, encoded.size() - 1}) {
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(encoded.data()), size);
        }
        auto truncated = wiz8::open_host_file(path);
        std::vector<bool> destination(32);
        SetBit(destination, 7);
        REQUIRE((!LoadPackedBits(truncated.get(), destination)));
        REQUIRE((destination.size() == 32 && TestBit(destination, 7)));
    }
    // Counts near 2^24 exposed the old float divide rounding into the next
    // word. Packed IO must use the integer word count at these boundaries.
    for (const unsigned count : {31u, 32u, 33u, 16777247u, 16777248u, 16777249u}) {
        std::vector<bool> source(count), resized(1);
        SetBit(source, count - 1);
        auto output = wiz8::open_host_file(path, wiz8::OpenMode::replace);
        REQUIRE((SavePackedBits(output.get(), source)));
        output->seek(0, wiz8::SeekOrigin::begin);
        REQUIRE((LoadPackedBits(output.get(), resized) && resized.size() == count));
        REQUIRE((resized == source));
    }
    // Mismatched serialized bit count and Huffman word count cannot commit.
    encoded[4] = 32; // Original count was 64, with three decoded words.
    {
        auto output = wiz8::open_host_file(path, wiz8::OpenMode::replace);
        output->write(encoded.data(), encoded.size());
        output->seek(0, wiz8::SeekOrigin::begin);
        std::vector<bool> destination(32);
        SetBit(destination, 7);
        REQUIRE((!LoadPackedBits(output.get(), destination) && TestBit(destination, 7)));
    }
    std::filesystem::remove_all(root);
    puts("ok: legacy packed-bit bytes, Huffman round trip and truncation");


}
