#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <vector>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "temporary_directory.h"
#include "wiz8/filesystem.h"
#include "wiz8/packed_bits.h"

TEST_CASE("packed bit IO")
{
    ScopedTemporaryDirectory directory{"bit-array-io"};
    const auto path = directory.path / "bits.bin";
    const unsigned int words[] = {0x81, 0x12345678, 0};
    std::vector<bool> original(64);
    for (unsigned bit = 0; bit < original.size(); ++bit)
        original[bit] = (words[bit / 32] >> (bit % 32)) & 1;
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        REQUIRE((SavePackedBits(*file, original)));
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
    auto file = wiz8::open_file(path);
    std::vector<unsigned char> encoded(file->size());
    file->read_exact(encoded.data(), encoded.size());
    // Captured from BitArray::Save at c8b831f, before the vector migration.
    const std::vector<unsigned char> legacy = {
        0x0d, 0xd0, 0xad, 0xde, 0x40, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x00, 0xdd, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xc0, 0x40,
        0x00, 0x00, 0x10, 0xcf, 0x8a, 0x46, 0x36, 0x0d, 0xd0, 0xad, 0xde,
    };
    {
        auto captured = wiz8::open_file(directory.path / "legacy.bin", wiz8::OpenMode::replace);
        captured->write(legacy.data(), legacy.size());
        captured->seek(0, wiz8::SeekOrigin::begin);
        std::vector<bool> restored;
        REQUIRE(LoadPackedBits(*captured, restored));
        REQUIRE(restored == original);
    }
    file->seek(0, wiz8::SeekOrigin::begin);
    std::vector<bool> restored(1);
    REQUIRE((LoadPackedBits(*file, restored) && restored.size() == 64));
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
        auto truncated = wiz8::open_file(path);
        std::vector<bool> destination(32);
        SetBit(destination, 7);
        REQUIRE((!LoadPackedBits(*truncated, destination)));
        REQUIRE((destination.size() == 32 && TestBit(destination, 7)));
    }
    // Invalid tree sizes must fail before allocating or following child pointers.
    for (const std::uint32_t symbols : {0u, 1u, 0xffffffffu}) {
        auto damaged = encoded;
        for (unsigned byte = 0; byte < 4; ++byte) damaged[12 + byte] = symbols >> (byte * 8);
        auto output = wiz8::open_file(path, wiz8::OpenMode::replace);
        output->write(damaged.data(), damaged.size());
        output->seek(0, wiz8::SeekOrigin::begin);
        std::vector<bool> destination(32);
        SetBit(destination, 7);
        REQUIRE_FALSE(LoadPackedBits(*output, destination));
        REQUIRE(destination.size() == 32);
        REQUIRE(TestBit(destination, 7));
    }
    // Counts near 2^24 exposed the old float divide rounding into the next
    // word. Packed IO must use the integer word count at these boundaries.
    for (const unsigned count : {1u, 31u, 32u, 33u, 16777247u}) {
        std::vector<bool> source(count), resized(1);
        SetBit(source, count - 1);
        auto output = wiz8::open_file(path, wiz8::OpenMode::replace);
        REQUIRE((SavePackedBits(*output, source)));
        output->seek(0, wiz8::SeekOrigin::begin);
        REQUIRE((LoadPackedBits(*output, resized) && resized.size() == count));
        REQUIRE((resized == source));
    }
    // Mismatched serialized bit count and Huffman word count cannot commit.
    encoded[4] = 32; // Original count was 64, with three decoded words.
    {
        auto output = wiz8::open_file(path, wiz8::OpenMode::replace);
        output->write(encoded.data(), encoded.size());
        output->seek(0, wiz8::SeekOrigin::begin);
        std::vector<bool> destination(32);
        SetBit(destination, 7);
        REQUIRE((!LoadPackedBits(*output, destination) && TestBit(destination, 7)));
    }
}

TEST_CASE("packed bits decode captured full width words")
{
    ScopedTemporaryDirectory directory{"packed-captured"};
    auto file = wiz8::open_file(directory.path / "captured.bin", wiz8::OpenMode::replace);
    const unsigned char golden[] = {0x06, 0x00, 0x00, 0x00, 0x60, 0x02, 0x00, 0x00, 0x00, 0x2e,
                                    0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0xd0, 0x01, 0x00,
                                    0x00, 0xe0, 0xff, 0xff, 0xff, 0xbf, 0x0d, 0x00, 0x00, 0x00,
                                    0x15, 0x00, 0x00, 0x00, 0x1e, 0xec, 0x64};

    REQUIRE(SDL_WriteU32LE(file->io(), 0xdeadd00d));
    REQUIRE(SDL_WriteU32LE(file->io(), 287));
    REQUIRE(SDL_WriteU32LE(file->io(), sizeof(golden)));
    file->write(golden, sizeof(golden));
    REQUIRE(SDL_WriteU32LE(file->io(), 0xdeadd00d));
    file->seek(0, wiz8::SeekOrigin::begin);
    std::vector<bool> decoded;
    REQUIRE(LoadPackedBits(*file, decoded));
    const std::uint32_t words[] = {10, 10, 11, 12, 13, 10, 13, 14, 0xffffffff};
    REQUIRE(decoded.size() == 287);
    for (std::size_t bit = 0; bit < decoded.size(); ++bit)
        REQUIRE(decoded[bit] == bool((words[bit / 32] >> (bit % 32)) & 1));
}

TEST_CASE("packed bits round trips word widths and repeated symbols")
{
    ScopedTemporaryDirectory directory{"packed-widths"};
    const auto path = directory.path / "bits.bin";
    auto roundTrip = [&](const std::vector<std::uint32_t>& words) {
        std::vector<bool> source(words.size() * 32 - 1), decoded;
        for (std::size_t bit = 0; bit < source.size(); ++bit)
            source[bit] = (words[bit / 32] >> (bit % 32)) & 1;
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        REQUIRE(SavePackedBits(*file, source));
        file->seek(0, wiz8::SeekOrigin::begin);
        REQUIRE(LoadPackedBits(*file, decoded));
        REQUIRE(decoded == source);
    };
    // Three leaves vary tree and payload alignment; width 32 exercises bit 31
    // and all-ones words. The last word's unused high bit is deliberately clear.
    for (unsigned width = 1; width <= 32; ++width) {
        const auto maximum = std::uint32_t((std::uint64_t(1) << width) - 1);
        roundTrip({0, 1, maximum, 0, maximum, 1, 0});
    }
    roundTrip({0, 0, 0}); // A single-symbol tree has no encoded payload bits.
    std::vector<std::uint32_t> words;
    for (std::uint32_t i = 0; i < 512; ++i) words.push_back(i * 1024);
    for (std::uint32_t i = 0; i < 1024; ++i) words.push_back((i % 7) * 1024);
    words.push_back(0);
    roundTrip(words);
}
