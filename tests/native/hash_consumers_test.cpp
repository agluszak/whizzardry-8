#include "surrender/srHuffman.h"
#include "surrender/srGERD.h"
#include "surrender/srCore.h"

#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <vector>

#define CHECK(e) do { if (!(e)) throw std::runtime_error(#e); } while (0)

static int checkConsumers() try
{
    // First-seen symbol order, repeated symbols (including index zero), and
    // enough distinct values to grow/rehash the lookup several times.
    std::vector<w8_ulong> values;
    srHuffman::Sampler sampler;
    for (w8_ulong i = 0; i < 512; ++i) {
        const auto symbol = i * 1024;
        values.push_back(symbol);
        sampler.insert(symbol);
    }
    for (w8_ulong i = 0; i < 1024; ++i) {
        const auto symbol = (i % 7) * 1024;
        values.push_back(symbol);
        sampler.insert(symbol);
    }
    CHECK(sampler.getNumSymbols() == 512);
    for (w8_ulong i = 0; i < 512; ++i) {
        CHECK(sampler.getSymbolValue(i) == i * 1024);
        CHECK(sampler.getSymbolFrequency(i) == 1 + (i < 7 ? 146 + (i < 2) : 0));
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
    uint64_t hash = 14695981039346656037ull;
    for (Sint64 i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
    // Captured before replacing srHashTable; protects tie-breaking and wire bytes.
    CHECK(size == 2470 && hash == 9655952702272475046ull);
    CHECK(SDL_SeekIO(io.get(), 0, SDL_IO_SEEK_SET) == 0);
    srHuffman::BitIStream input(io.get());
    srHuffman::Decompressor decoder(input);
    CHECK(decoder.getDataCount() == values.size());
    for (auto symbol : values) CHECK(decoder.decompressSymbol() == symbol);

    // Interning must distinguish both texture addresses and the shader word,
    // preserve index zero, and keep existing indices across growth and clear.
    srGERD::Renderer::TextureSetCache cache;
    struct Texture : srTexture {
        srClass* vInstance() override { return new Texture; }
        void invalidate() override {}
        void setupDefaultValues() override {}
    } textures[2];
    srGERD::Renderer::TextureSetKey key{nullptr, nullptr, {}};
    for (w8_ulong i = 0; i < 512; ++i) {
        key.shader.value = i;
        CHECK(cache.intern(key) == i);
    }
    for (w8_ulong i = 0; i < 512; ++i) {
        key.shader.value = i;
        CHECK(cache.intern(key) == i);
        CHECK(cache.sets[i].shader.value == i);
        const auto blend = (i >> srShader::DSTBLEND_SHIFT) & 7;
        CHECK(cache.sets[i].blend == (blend == 1 ? 2 : blend == 2 || blend == 3 ? 3 :
                                     blend == 4 || blend == 5 ? 1 : 0));
    }
    key.shader.value = 0;
    key.texture0 = &textures[0];
    CHECK(cache.intern(key) == 512);
    key.texture0 = &textures[1];
    CHECK(cache.intern(key) == 513);
    key.texture0 = nullptr;
    key.texture1 = &textures[0];
    CHECK(cache.intern(key) == 514);
    CHECK(cache.sets.size() == 515);
    CHECK(cache.sets[512].texture0 == &textures[0]);
    CHECK(cache.sets[514].texture1 == &textures[0]);
    cache.clear();
    CHECK(cache.sets.empty() && cache.intern(key) == 0 && cache.intern(key) == 0);
    std::puts("ok: Huffman wire compatibility and renderer texture-set interning");
    return 0;
}
catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }

int main()
{
    if (!srInit()) return 1;
    const auto result = checkConsumers();
    return srExit() ? result : 1;
}
