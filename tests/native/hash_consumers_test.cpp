#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "surrender/srHuffman.h"
#include "surrender/srGERD.h"
#include "surrender/srCore.h"
#include "compat/compiler.h"
#include "surrender/srIO.h"
#include "surrender/srShader.h"
#include "surrender/srTexture.h"


static void checkConsumers()
{
    // First-seen symbol order, repeated symbols (including index zero), and
    // enough distinct values to grow/rehash the lookup several times.
    std::vector<std::uint32_t> values;
    srHuffman::Sampler sampler;
    for (std::uint32_t i = 0; i < 512; ++i) {
        const auto symbol = i * 1024;
        values.push_back(symbol);
        sampler.insert(symbol);
    }
    for (std::uint32_t i = 0; i < 1024; ++i) {
        const auto symbol = (i % 7) * 1024;
        values.push_back(symbol);
        sampler.insert(symbol);
    }
    REQUIRE(sampler.getNumSymbols() == 512);
    for (std::uint32_t i = 0; i < 512; ++i) {
        REQUIRE(sampler.getSymbolValue(i) == i * 1024);
        REQUIRE(sampler.getSymbolFrequency(i) == 1 + (i < 7 ? 146 + (i < 2) : 0));
    }
    srHuffman::Compressor compressor(sampler);
    auto io = srIO::own(SDL_IOFromDynamicMem());
    {
        srHuffman::BitOStream output(io.get());
        output.put(compressor.num_symbols, 32);
        output.put(compressor.code_width, 6);
        output.put(values.size(), 32);
        compressor.storeSymbolTable(output);
        for (auto symbol : values) compressor.compressSymbol(output, symbol);
        output.flush();
    }
    const auto size = SDL_GetIOSize(io.get());
    const auto* bytes = static_cast<const Uint8*>(SDL_GetPointerProperty(
        SDL_GetIOProperties(io.get()), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
    std::uint64_t hash = 14695981039346656037ull;
    for (Sint64 i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
    // Captured before replacing srHashTable; protects tie-breaking and wire bytes.
    REQUIRE((size == 2470 && hash == 9655952702272475046ull));
    REQUIRE(SDL_SeekIO(io.get(), 0, SDL_IO_SEEK_SET) == 0);
    srHuffman::BitIStream input(io.get());
    srHuffman::Decompressor decoder(input);
    REQUIRE(decoder.getDataCount() == values.size());
    for (auto symbol : values) REQUIRE(decoder.decompressSymbol() == symbol);

    // Interning must distinguish both texture addresses and the shader word,
    // preserve index zero, and keep existing indices across growth and clear.
    srGERD::Renderer::TextureSetCache cache;
    struct Texture : srTexture {
        void invalidate() override {}
        void setupDefaultValues() override {}
    } textures[2];
    srGERD::Renderer::TextureSetKey key{nullptr, nullptr, {}};
    for (std::uint32_t i = 0; i < 512; ++i) {
        key.shader.value = i;
        REQUIRE(cache.intern(key) == i);
    }
    for (std::uint32_t i = 0; i < 512; ++i) {
        key.shader.value = i;
        REQUIRE(cache.intern(key) == i);
        REQUIRE(cache.sets[i].shader.value == i);
        const auto blend = (i >> srShader::DSTBLEND_SHIFT) & 7;
        REQUIRE((cache.sets[i].blend == (blend == 1 ? 2 : blend == 2 || blend == 3 ? 3 :
                                     blend == 4 || blend == 5 ? 1 : 0)));
    }
    key.shader.value = 0;
    key.texture0 = &textures[0];
    REQUIRE(cache.intern(key) == 512);
    key.texture0 = &textures[1];
    REQUIRE(cache.intern(key) == 513);
    key.texture0 = nullptr;
    key.texture1 = &textures[0];
    REQUIRE(cache.intern(key) == 514);
    REQUIRE(cache.sets.size() == 515);
    REQUIRE(cache.sets[512].texture0 == &textures[0]);
    REQUIRE(cache.sets[514].texture1 == &textures[0]);
    cache.clear();
    REQUIRE((cache.sets.empty() && cache.intern(key) == 0 && cache.intern(key) == 0));
    std::puts("ok: Huffman wire compatibility and renderer texture-set interning");
}

TEST_CASE("hash_consumers_test")
{
    REQUIRE(srInit());
    checkConsumers();
    REQUIRE(srExit());
}
