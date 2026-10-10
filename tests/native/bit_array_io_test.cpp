#include <catch2/catch_test_macros.hpp>
#include "wiz8/engine_code/BitArray.h"
#include "surrender/srIO.h"
#include "temporary_directory.h"

#include <cstdio>
#include <fstream>
#include <vector>


TEST_CASE("bit_array_io_test")
{
    const auto root = std::filesystem::path(make_temporary_directory("bit-array-io"));
    const auto path = root / "bits.bin";
    const unsigned int words[] = {0x81, 0x12345678, 0};
    BitArray original(64);
    std::copy(std::begin(words), std::end(words), original.puiIndex);
    {
        auto file = wiz8::open_host_file(path, wiz8::OpenMode::replace);
        REQUIRE(original.Save(file.get()));
        REQUIRE(file->seek(0, wiz8::SeekOrigin::begin) == 0);
        Uint32 magic;
        REQUIRE((SDL_ReadU32LE(file->io(), &magic) && magic == 0xdeadd00d));
        Uint32 count, packed_size;
        REQUIRE((SDL_ReadU32LE(file->io(), &count) && count == 64));
        REQUIRE(SDL_ReadU32LE(file->io(), &packed_size));
        REQUIRE(packed_size + 16 == file->size());
        REQUIRE(SDL_SeekIO(file->io(), -4, SDL_IO_SEEK_END) == file->size() - 4);
        REQUIRE((SDL_ReadU32LE(file->io(), &magic) && magic == 0xdeadd00d));
    }
    auto file = wiz8::open_host_file(path);
    std::vector<unsigned char> encoded(file->size());
    file->read_exact(encoded.data(), encoded.size());
    file->seek(0, wiz8::SeekOrigin::begin);
    BitArray restored(1);
    REQUIRE((restored.Load(file.get()) && restored.bit_count == 64 && restored.word_count == 3));
    REQUIRE(std::equal(std::begin(words), std::end(words), restored.puiIndex));
    file->close();
    // Missing framing and Huffman bytes fail without changing the destination.
    for (const auto size : {size_t(0), size_t(12), encoded.size() - 4, encoded.size() - 1}) {
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(encoded.data()), size);
        }
        auto truncated = wiz8::open_host_file(path);
        BitArray destination(32);
        destination.Set(7);
        REQUIRE(!destination.Load(truncated.get()));
        REQUIRE((destination.bit_count == 32 && destination.Test(7)));
    }
    // Counts near 2^24 exposed the old float divide rounding into the next
    // word. Constructor, SetSize and Load must agree on their actual allocation.
    for (const unsigned count : {31u, 32u, 33u, 16777247u, 16777248u, 16777249u}) {
        BitArray source(count), resized(1);
        resized.SetSize(count);
        REQUIRE((source.word_count == count / 32 + 1 && resized.word_count == source.word_count));
        REQUIRE(source.tail_mask == resized.tail_mask);
        source.Set(count - 1);
        auto output = wiz8::open_host_file(path, wiz8::OpenMode::replace);
        REQUIRE(source.Save(output.get()));
        output->seek(0, wiz8::SeekOrigin::begin);
        REQUIRE((resized.Load(output.get()) && resized.bit_count == count));
        REQUIRE((resized.word_count == source.word_count && resized.Test(count - 1)));
    }
    // Mismatched serialized bit count and Huffman word count cannot commit.
    encoded[4] = 32; // Original count was 64, with three decoded words.
    {
        auto output = wiz8::open_host_file(path, wiz8::OpenMode::replace);
        output->write(encoded.data(), encoded.size());
        output->seek(0, wiz8::SeekOrigin::begin);
        BitArray destination(32);
        destination.Set(7);
        REQUIRE((!destination.Load(output.get()) && destination.Test(7)));
    }
    std::filesystem::remove_all(root);
    puts("ok: SDL BitArray framing, Huffman round trip and truncation");
    

}
