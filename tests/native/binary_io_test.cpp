#include <array>
#include <span>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "surrender/srHuffman.h"

TEST_CASE("Huffman bit IO bounds and full width words")
{
    const unsigned char data[] = {0x81, 0x42};
    srHuffman::BitIStream input(data);
    REQUIRE(input.get(9) == 0x81);
    REQUIRE_THROWS_AS(input.get(32), std::out_of_range);
    REQUIRE(input.get(7) == 0x21); // Rejected reads leave the cursor intact.
    REQUIRE(input.peekByte() == 0); // Lookahead pads; consumption rejects truncation.
    REQUIRE_THROWS_AS(input.getBit(), std::out_of_range);
    REQUIRE(input.get(0) == 0);
    REQUIRE_THROWS(input.get(33));

    // Every alignment must preserve bit 31 without shifting by the word width.
    for (unsigned offset = 0; offset < 8; ++offset) {
        srHuffman::BitOStream output;
        output.put(0x55, offset);
        for (auto word : {0u, 0x80000000u, 0xffffffffu, 0x12345678u}) output.put(word, 32);
        srHuffman::BitIStream reader(output.bytes);
        REQUIRE(reader.get(offset) == (0x55u & ((1u << offset) - 1)));
        for (auto word : {0u, 0x80000000u, 0xffffffffu, 0x12345678u})
            REQUIRE(reader.get(32) == word);
    }
    // Crossing the old cache boundary needs no special handling.
    std::array<unsigned char, 130> bytes{};
    bytes[127] = 0x81;
    bytes[128] = 0x42;
    bytes[129] = 0xff;
    srHuffman::BitIStream reader(bytes);
    for (unsigned i = 0; i < 127; ++i) REQUIRE(reader.get(8) == 0);
    REQUIRE(reader.get(24) == 0xff4281);
    REQUIRE(reader.peekByte() == 0);
}

TEST_CASE("Huffman captured tree decoding and round trip")
{
    // Exercise a branching symbol tree, as used by octree alpha bits.
    const w8_ulong values[] = {10, 10, 11, 12, 13, 10, 13, 14, 0xffffffff};
    srHuffman::Sampler tree_sampler;
    for (auto value : values) ++tree_sampler[value];
    srHuffman::Compressor compressor(tree_sampler);
    srHuffman::BitOStream encoded;
    encoded.put(compressor.num_symbols, 32);
    encoded.put(compressor.code_width, 6);
    encoded.put(sizeof(values) / sizeof(values[0]), 32);
    compressor.storeSymbolTable(encoded);
    for (auto value : values) compressor.compressSymbol(encoded, value);
    // Captured from main's pre-consolidation stream/Huffman implementation.
    const unsigned char golden[] = {0x06, 0x00, 0x00, 0x00, 0x60, 0x02, 0x00, 0x00, 0x00, 0x2e,
                                    0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0xd0, 0x01, 0x00,
                                    0x00, 0xe0, 0xff, 0xff, 0xff, 0xbf, 0x0d, 0x00, 0x00, 0x00,
                                    0x15, 0x00, 0x00, 0x00, 0x1e, 0xec, 0x64};
    for (const auto data :
         {std::span<const unsigned char>(golden), std::span<const unsigned char>(encoded.bytes)}) {
        srHuffman::BitIStream bits(data);
        srHuffman::Decompressor decoder(bits);
        REQUIRE(decoder.getDataCount() == sizeof(values) / sizeof(values[0]));
        for (auto value : values) REQUIRE(decoder.decompressSymbol() == value);
    }
}

TEST_CASE("Huffman repeated symbols and frequency map growth")
{
    // Repeated symbols (including zero), and
    // enough distinct values to grow/rehash the lookup several times.
    std::vector<w8_ulong> values;
    srHuffman::Sampler sampler;
    for (w8_ulong i = 0; i < 512; ++i) {
        const auto symbol = i * 1024;
        values.push_back(symbol);
        ++sampler[symbol];
    }
    for (w8_ulong i = 0; i < 1024; ++i) {
        const auto symbol = (i % 7) * 1024;
        values.push_back(symbol);
        ++sampler[symbol];
    }
    REQUIRE(sampler.size() == 512);
    for (w8_ulong i = 0; i < 512; ++i) {
        REQUIRE(sampler.at(i * 1024) == 1 + (i < 7 ? 146 + (i < 2) : 0));
    }
    srHuffman::Compressor compressor(sampler);
    srHuffman::BitOStream output;
    output.put(compressor.num_symbols, 32);
    output.put(compressor.code_width, 6);
    output.put(values.size(), 32);
    compressor.storeSymbolTable(output);
    for (auto symbol : values) compressor.compressSymbol(output, symbol);
    srHuffman::BitIStream input(output.bytes);
    srHuffman::Decompressor decoder(input);
    REQUIRE(decoder.getDataCount() == values.size());
    for (auto symbol : values) REQUIRE(decoder.decompressSymbol() == symbol);
}

TEST_CASE("Huffman single symbol trees need no payload bits")
{
    for (const auto value : {0u, 0xffffffffu}) {
        srHuffman::Sampler sampler{{value, 3}};
        srHuffman::Compressor compressor(sampler);
        srHuffman::BitOStream output;
        output.put(compressor.num_symbols, 32);
        output.put(compressor.code_width, 6);
        output.put(3, 32);
        compressor.storeSymbolTable(output);
        for (unsigned i = 0; i < 3; ++i) compressor.compressSymbol(output, value);
        srHuffman::BitIStream input(output.bytes);
        srHuffman::Decompressor decoder(input);
        REQUIRE(decoder.getDataCount() == 3);
        for (unsigned i = 0; i < 3; ++i) REQUIRE(decoder.decompressSymbol() == value);
    }
}
